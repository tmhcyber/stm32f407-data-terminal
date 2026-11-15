"""Decode the recorded 8E1 UARTs and measure the RS485 direction envelope.

Consumes Saleae transition CSV plus the matching PC packet log. No hardware IO.
The recorded bench wiring is D0=PA3/RX, D1=PA2/TX, D2=PC0/DE.
"""
import argparse
from bisect import bisect_right
import csv
import json
from pathlib import Path


def analyze(folder):
    with (folder / 'digital.csv').open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    times = [float(row['Time [s]']) for row in rows]
    signals = {channel: [int(row[f'Channel {channel}']) for row in rows] for channel in range(3)}
    bit = 1 / 115200
    def level(channel, at):
        return signals[channel][max(0, bisect_right(times, at) - 1)]
    def decode(channel):
        frames, errors = [], []
        earliest = -1.0
        for index in range(1, len(rows)):
            if signals[channel][index-1] != 1 or signals[channel][index] != 0:
                continue
            start = times[index]
            if start < earliest:
                continue
            bits = [level(channel, start + (k + .5) * bit) for k in range(11)]
            value = sum(bits[k+1] << k for k in range(8))
            if bits[0] != 0 or bits[10] != 1 or (sum(bits[1:10]) % 2):
                errors.append(dict(start_s=start, bits=bits))
            frames.append(dict(start_s=start, stop_start_s=start+10*bit,
                               stop_end_s=start+11*bit, value=value))
            earliest = start + 10.75 * bit
        return frames, errors
    requests, rx_errors = decode(0)
    responses, tx_errors = decode(1)
    def fit_bit(frames, channel):
        pairs = []
        for frame in frames:
            value = frame['value']
            bits = [0] + [(value >> k) & 1 for k in range(8)]
            bits += [sum(bits[1:]) % 2, 1]
            for i in range(bisect_right(times, frame['start_s']), bisect_right(times, frame['start_s']+10.3*bit)):
                if signals[channel][i] == signals[channel][i-1]:
                    continue
                elapsed = times[i]-frame['start_s']
                k = round(elapsed/bit)
                if 1 <= k <= 10 and bits[k] != bits[k-1] and abs(elapsed-k*bit) < .25*bit:
                    pairs.append((k,elapsed))
        fitted = sum(k*elapsed for k,elapsed in pairs)/sum(k*k for k,_ in pairs)
        return fitted, len(pairs)
    rx_bit, rx_fit_edges = fit_bit(requests,0)
    tx_bit, tx_fit_edges = fit_bit(responses,1)
    for frames, period in ((requests,rx_bit),(responses,tx_bit)):
        for frame in frames:
            frame.update(stop_start_s=frame['start_s']+10*period,stop_end_s=frame['start_s']+11*period)
    packets = [json.loads(line) for line in (folder / 'serial/packets.jsonl').read_text().splitlines()]
    expected_rx = b''.join(bytes.fromhex(p['chunk_hex']) for p in packets if p['direction']=='tx')
    expected_tx = b''.join(bytes.fromhex(p['chunk_hex']) for p in packets if p['direction']=='rx')
    got_rx = bytes(frame['value'] for frame in requests)
    got_tx = bytes(frame['value'] for frame in responses)
    rises = [times[i] for i in range(1,len(rows)) if signals[2][i-1]==0 and signals[2][i]==1]
    falls = [times[i] for i in range(1,len(rows)) if signals[2][i-1]==1 and signals[2][i]==0]
    measurements=[]
    if len(rises)==len(falls):
        for rise,fall in zip(rises,falls):
            tx = [frame for frame in responses if rise <= frame['start_s'] < fall]
            if not tx:
                continue
            before = [frame for frame in requests if frame['stop_end_s'] < tx[0]['start_s']]
            measurements.append(dict(de_rise_s=rise,tx_start_s=tx[0]['start_s'],
                last_byte_start_s=tx[-1]['start_s'],last_stop_end_s=tx[-1]['stop_end_s'],de_fall_s=fall,
                tx_bytes=len(tx),de_lead_us=(tx[0]['start_s']-rise)*1e6,
                release_after_stop_us=(fall-tx[-1]['stop_end_s'])*1e6,
                request_to_response_silence_us=(tx[0]['start_s']-before[-1]['stop_end_s'])*1e6 if before else None,
                de_width_us=(fall-rise)*1e6))
    passed = bool(not rx_errors and not tx_errors and got_rx==expected_rx and got_tx==expected_tx and
        len(measurements)==10 and all(x['tx_bytes']==11 and x['de_lead_us']>=0 and
        x['release_after_stop_us']>=0 and x['request_to_response_silence_us']>=1750 for x in measurements))
    return dict(passed=passed,sample_rate_hz=8000000,sample_resolution_us=.125,uart='115200 8E1',
        mapping={'D0':'PA3 request RX','D1':'PA2 response TX','D2':'PC0 direction'},
        decoded_request_bytes=len(got_rx),decoded_response_bytes=len(got_tx),
        request_bytes_match=got_rx==expected_rx,response_bytes_match=got_tx==expected_tx,
        rx_bit_us=rx_bit*1e6,tx_bit_us=tx_bit*1e6,rx_fit_edges=rx_fit_edges,tx_fit_edges=tx_fit_edges,
        rx_errors=rx_errors,tx_errors=tx_errors,measurements=measurements,
        stop_end_method='last start edge + 11 bit periods fitted from decoded UART edges in the same capture; sampling clock not calibrated'), times, signals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder',type=Path)
    parser.add_argument('--plot',action='store_true',help='also export a two-panel timing figure (requires matplotlib)')
    args = parser.parse_args()
    result,times,signals=analyze(args.folder)
    (args.folder/'timing-analysis.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))
    if args.plot:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        measure=result['measurements'][0]
        origin=measure['tx_start_s']
        fig,axes=plt.subplots(2,1,figsize=(11,6.3),layout='constrained')
        ranges=[(origin-.0031,measure['de_fall_s']+.0003),
                (measure['last_byte_start_s']-.00001,measure['de_fall_s']+.000012)]
        colors=['#3676ba','#dd7930','#38935e']
        for ax,(left,right) in zip(axes,ranges):
            a=max(0,bisect_right(times,left)-1);b=min(len(times),bisect_right(times,right)+1)
            for channel,label in enumerate(('PA3 / RX request','PA2 / TX response','PC0 / direction')):
                xs=[(t-origin)*1e6 for t in times[a:b]]+[ (right-origin)*1e6 ]
                ys=[signals[channel][i]*.65+(2-channel)*1.1 for i in range(a,b)]
                ax.step(xs,ys+[ys[-1]],where='post',label=label,color=colors[channel],linewidth=1.2)
            ax.set_xlim((left-origin)*1e6,(right-origin)*1e6)
            ax.set_yticks([2.5,1.4,.3],['RX','TX','DE'])
            ax.grid(axis='x',alpha=.25);ax.set_xlabel('Time relative to response start (us)')
        axes[0].set_title('Modbus RTU 115200 8E1: request, response and direction (8 MS/s)')
        axes[0].legend(loc='upper left',ncol=3,fontsize=8)
        stop=(measure['last_stop_end_s']-origin)*1e6
        fall=(measure['de_fall_s']-origin)*1e6
        axes[1].axvline(stop,color='#777',linestyle='--',label='Fitted stop-bit end')
        axes[1].axvline(fall,color='#38935e',linestyle=':')
        axes[1].annotate(f"DE released {measure['release_after_stop_us']:.2f} us after stop end",
                        xy=(fall,.65),xytext=(stop-60,2.85),arrowprops=dict(arrowstyle='->'),fontsize=9)
        axes[1].set_title('Last response byte and stop-bit completion')
        fig.savefig(args.folder/'timing.png',dpi=180)
        plt.close(fig)
    return 0 if result['passed'] else 1


if __name__=='__main__':
    raise SystemExit(main())
