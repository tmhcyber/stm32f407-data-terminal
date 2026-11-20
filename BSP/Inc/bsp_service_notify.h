#ifndef BSP_SERVICE_NOTIFY_H
#define BSP_SERVICE_NOTIFY_H

#if APP_RTOS_ENABLED
/* Defined by the scheduler adapter. BSP keeps ownership of the actual events. */
void BSP_ServiceNotify_CommunicationFromISR(void);
void BSP_ServiceNotify_UplinkFromISR(void);
#else
#define BSP_ServiceNotify_CommunicationFromISR() ((void)0)
#define BSP_ServiceNotify_UplinkFromISR() ((void)0)
#endif

#endif
