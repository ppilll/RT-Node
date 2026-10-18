#ifndef RT_NODE_ADC_APP_H
#define RT_NODE_ADC_APP_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t raw;
    uint16_t avg;
    uint16_t mv; /* Estimate only: assumes VDDA = 3300 mV. */
    uint32_t n;  /* Accepted samples, in steps of 32; uint32 wrap is intentional. */
    uint32_t missed; /* Discarded complete half-windows. */
    bool valid;
} AdcApp_Result;

typedef enum {
    ADC_APP_NOT_READY = 0,
    ADC_APP_READY,
    ADC_APP_RUNNING,
    ADC_APP_INIT_ERROR,
    ADC_APP_START_ERROR,
    ADC_APP_DMA_ERROR,
    ADC_APP_STALLED
} AdcApp_Status;

/* Main, once, with the HAL startup tick running. Failure is ADC-local. */
bool AdcApp_Init(void);
/* UartTask, once, after RX is available and scheduler has started. */
bool AdcApp_Start(void);
/* Only UartTask owns processing, status reporting and cached snapshots. */
void AdcApp_Process(void);
bool AdcApp_GetLatest(AdcApp_Result *out);
AdcApp_Status AdcApp_GetStatus(void);
void DMA1_Channel1_IRQHandler(void);

#endif
