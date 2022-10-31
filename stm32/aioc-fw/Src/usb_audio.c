#include "usb_audio.h"
#include "tusb.h"
#include "stm32f3xx_hal.h"

#ifndef AUDIO_SAMPLE_RATE
#define AUDIO_SAMPLE_RATE   48000
#endif
// Audio controls
// Current states
bool mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];                        // +1 for master channel 0
uint16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX + 1];                    // +1 for master channel 0
uint32_t sampFreq;
uint8_t clkValid;

// Range states
audio_control_range_2_n_t(1) volumeRng[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX+1];           // Volume range state
audio_control_range_4_n_t(1) sampleFreqRng;                         // Sample frequency range state

// Audio test data
uint16_t test_buffer_audio[CFG_TUD_AUDIO_EP_SZ_IN/2];
uint16_t startVal = 0;


//--------------------------------------------------------------------+
// Application Callback API Implementations
//--------------------------------------------------------------------+

// Invoked when audio class specific set request received for an EP
bool tud_audio_set_req_ep_cb(uint8_t rhport, tusb_control_request_t const * p_request, uint8_t *pBuff)
{
  (void) rhport;
  (void) pBuff;

  // We do not support any set range requests here, only current value requests
  TU_VERIFY(p_request->bRequest == AUDIO_CS_REQ_CUR);

  // Page 91 in UAC2 specification
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t ep = TU_U16_LOW(p_request->wIndex);

  (void) channelNum; (void) ctrlSel; (void) ep;

  return false;     // Yet not implemented
}

// Invoked when audio class specific set request received for an interface
bool tud_audio_set_req_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request, uint8_t *pBuff)
{
  (void) rhport;
  (void) pBuff;

  // We do not support any set range requests here, only current value requests
  TU_VERIFY(p_request->bRequest == AUDIO_CS_REQ_CUR);

  // Page 91 in UAC2 specification
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t itf = TU_U16_LOW(p_request->wIndex);

  (void) channelNum; (void) ctrlSel; (void) itf;

  return false;     // Yet not implemented
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const * p_request, uint8_t *pBuff)
{
  (void) rhport;

  // Page 91 in UAC2 specification
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t itf = TU_U16_LOW(p_request->wIndex);
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  (void) itf;

  // We do not support any set range requests here, only current value requests
  TU_VERIFY(p_request->bRequest == AUDIO_CS_REQ_CUR);

  // If request is for our feature unit
  if ( entityID == 2 )
  {
    switch ( ctrlSel )
    {
      case AUDIO_FU_CTRL_MUTE:
        // Request uses format layout 1
        TU_VERIFY(p_request->wLength == sizeof(audio_control_cur_1_t));

        mute[channelNum] = ((audio_control_cur_1_t*) pBuff)->bCur;

        TU_LOG2("    Set Mute: %d of channel: %u\r\n", mute[channelNum], channelNum);
      return true;

      case AUDIO_FU_CTRL_VOLUME:
        // Request uses format layout 2
        TU_VERIFY(p_request->wLength == sizeof(audio_control_cur_2_t));

        volume[channelNum] = (uint16_t) ((audio_control_cur_2_t*) pBuff)->bCur;

        TU_LOG2("    Set Volume: %d dB of channel: %u\r\n", volume[channelNum], channelNum);
      return true;

        // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
      return false;
    }
  }
  return false;    // Yet not implemented
}

// Invoked when audio class specific get request received for an EP
bool tud_audio_get_req_ep_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void) rhport;

  // Page 91 in UAC2 specification
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t ep = TU_U16_LOW(p_request->wIndex);

  (void) channelNum; (void) ctrlSel; (void) ep;

  //    return tud_control_xfer(rhport, p_request, &tmp, 1);

  return false;     // Yet not implemented
}

// Invoked when audio class specific get request received for an interface
bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void) rhport;

  // Page 91 in UAC2 specification
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  uint8_t itf = TU_U16_LOW(p_request->wIndex);

  (void) channelNum; (void) ctrlSel; (void) itf;

  return false;     // Yet not implemented
}

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void) rhport;

  // Page 91 in UAC2 specification
  uint8_t channelNum = TU_U16_LOW(p_request->wValue);
  uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
  // uint8_t itf = TU_U16_LOW(p_request->wIndex);           // Since we have only one audio function implemented, we do not need the itf value
  uint8_t entityID = TU_U16_HIGH(p_request->wIndex);

  // Input terminal (Microphone input)
  if (entityID == AUDIO_CTRL_ID_MIC_INPUT)
  {
    switch ( ctrlSel )
    {
      case AUDIO_TE_CTRL_CONNECTOR:
      {
        // The terminal connector control only has a get request with only the CUR attribute.
        audio_desc_channel_cluster_t ret;

        // Those are dummy values for now
        ret.bNrChannels = 1;
        ret.bmChannelConfig = 0;
        ret.iChannelNames = 0;

        TU_LOG2("    Get terminal connector\r\n");

        return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void*) &ret, sizeof(ret));
      }
      break;

        // Unknown/Unsupported control selector
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  // Feature unit
  if (entityID == AUDIO_CTRL_ID_MIC_FUNIT)
  {
    switch ( ctrlSel )
    {
      case AUDIO_FU_CTRL_MUTE:
        // Audio control mute cur parameter block consists of only one byte - we thus can send it right away
        // There does not exist a range parameter block for mute
        TU_LOG2("    Get Mute of channel: %u\r\n", channelNum);
        return tud_control_xfer(rhport, p_request, &mute[channelNum], 1);

      case AUDIO_FU_CTRL_VOLUME:
        switch ( p_request->bRequest )
        {
          case AUDIO_CS_REQ_CUR:
            TU_LOG2("    Get Volume of channel: %u\r\n", channelNum);
            return tud_control_xfer(rhport, p_request, &volume[channelNum], sizeof(volume[channelNum]));

          case AUDIO_CS_REQ_RANGE:
            TU_LOG2("    Get Volume range of channel: %u\r\n", channelNum);

            // Copy values - only for testing - better is version below
            audio_control_range_2_n_t(1)
            ret;

            ret.wNumSubRanges = 1;
            ret.subrange[0].bMin = -90;    // -90 dB
            ret.subrange[0].bMax = 90;      // +90 dB
            ret.subrange[0].bRes = 1;       // 1 dB steps

            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, (void*) &ret, sizeof(ret));

            // Unknown/Unsupported control
          default:
            TU_BREAKPOINT();
            return false;
        }
      break;

        // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  // Clock Source unit
  if ( entityID == AUDIO_CTRL_ID_CLOCK )
  {
    switch ( ctrlSel )
    {
      case AUDIO_CS_CTRL_SAM_FREQ:
        // channelNum is always zero in this case
        switch ( p_request->bRequest )
        {
          case AUDIO_CS_REQ_CUR:
            TU_LOG2("    Get Sample Freq.\r\n");
            return tud_control_xfer(rhport, p_request, &sampFreq, sizeof(sampFreq));

          case AUDIO_CS_REQ_RANGE:
            TU_LOG2("    Get Sample Freq. range\r\n");
            return tud_control_xfer(rhport, p_request, &sampleFreqRng, sizeof(sampleFreqRng));

           // Unknown/Unsupported control
          default:
            TU_BREAKPOINT();
            return false;
        }
      break;

      case AUDIO_CS_CTRL_CLK_VALID:
        // Only cur attribute exists for this request
        TU_LOG2("    Get Sample Freq. valid\r\n");
        return tud_control_xfer(rhport, p_request, &clkValid, sizeof(clkValid));

      // Unknown/Unsupported control
      default:
        TU_BREAKPOINT();
        return false;
    }
  }

  TU_LOG2("  Unsupported entity: %d\r\n", entityID);
  return false;     // Yet not implemented
}

#if 0
bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
  (void) rhport;
  (void) itf;
  (void) ep_in;
  (void) cur_alt_setting;

  tud_audio_write ((uint8_t *)test_buffer_audio, CFG_TUD_AUDIO_EP_SZ_IN);

  return true;
}


bool tud_audio_tx_done_post_load_cb(uint8_t rhport, uint16_t n_bytes_copied, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
  (void) rhport;
  (void) n_bytes_copied;
  (void) itf;
  (void) ep_in;
  (void) cur_alt_setting;

  for (size_t cnt = 0; cnt < CFG_TUD_AUDIO_EP_SZ_IN/2; cnt++)
  {
    test_buffer_audio[cnt] = startVal++;
  }

  return true;
}
#endif

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
    (void) rhport;
    (void) p_request;

    NVIC_EnableIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(ADC1_2_IRQn);

    return true;
}


bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void) rhport;
  (void) p_request;
  NVIC_DisableIRQ(TIM3_IRQn);
  NVIC_DisableIRQ(ADC1_2_IRQn);

  return true;
}

void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t* feedback_param)
{
    /* Configure parameters for feedback endpoint */
    feedback_param->frequency.mclk_freq = 2 * HAL_RCC_GetPCLK1Freq();
    feedback_param->sample_freq = AUDIO_SAMPLE_RATE;
    feedback_param->method = AUDIO_FEEDBACK_METHOD_FREQUENCY_FLOAT;
}

TU_ATTR_FAST_FUNC void tud_audio_feedback_interval_isr(uint8_t func_id, uint32_t frame_number, uint8_t interval_shift)
{
    static uint32_t prev_cycles = 0;
    uint32_t this_cycles = TIM2->CNT;

    /* Calculate number of master clock cycles between now and last call */
    uint32_t diff_cycles = (uint32_t) (((uint64_t) this_cycles - prev_cycles) & 0xFFFFFFFFUL);

    /* Notify the USB audio feedback endpoint */
    tud_audio_feedback_update(0, diff_cycles);

    /* Prepare for next time */
    prev_cycles = this_cycles;
}

void ADC1_2_IRQHandler (void)
{
    if (ADC2->ISR & ADC_ISR_EOS) {
        ADC2->ISR = ADC_ISR_EOS;
        uint16_t value = ADC2->DR;
        int16_t a =  ((int32_t) value - 32768) & 0xFFFFU;
        tud_audio_write (&a, sizeof(value));

        //uint16_t value = sine[startVal];
        //tud_audio_write (&value, sizeof(value));
        //startVal = startVal == (sizeof(sine)/sizeof(*sine)-1) ? 0 : startVal + 1;

        //uint16_t value = startVal++;
        //tud_audio_write(&value, sizeof(value));
    }
}

void TIM3_IRQHandler(void)
{
    if (TIM3->SR & TIM_SR_UIF) {
        TIM3->SR = (uint32_t) ~TIM_SR_UIF;

        uint16_t items = tud_audio_available() / 2;
        int16_t sample = 0x0000;
        if (items > 0) {
            /* Grab a sample from usb */
            tud_audio_read(&sample, sizeof(sample));
        }

        int16_t a =  ((int32_t) sample + 32768) & 0xFFFFU;

        /* Load DAC holding register with sample */
        DAC1->DHR12L1 = a;
    }
}

static void GPIO_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef ADCInGpio;
    ADCInGpio.Pin = GPIO_PIN_2;
    ADCInGpio.Mode = GPIO_MODE_ANALOG;
    ADCInGpio.Pull = GPIO_NOPULL;
    ADCInGpio.Speed = GPIO_SPEED_FREQ_LOW;
    ADCInGpio.Alternate = 0;
    HAL_GPIO_Init(GPIOB, &ADCInGpio);

    GPIO_InitTypeDef SamplerateGpio;
    SamplerateGpio.Pin = GPIO_PIN_0;
    SamplerateGpio.Mode = GPIO_MODE_AF_PP;
    SamplerateGpio.Pull = GPIO_NOPULL;
    SamplerateGpio.Speed = GPIO_SPEED_FREQ_HIGH;
    SamplerateGpio.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOB, &SamplerateGpio);

    GPIO_InitTypeDef DACOutGpio;
    DACOutGpio.Pin = GPIO_PIN_4;
    DACOutGpio.Mode = GPIO_MODE_ANALOG;
    DACOutGpio.Pull = GPIO_NOPULL;
    DACOutGpio.Speed = GPIO_SPEED_FREQ_LOW;
    DACOutGpio.Alternate = 0;
    HAL_GPIO_Init(GPIOA, &DACOutGpio);
}

static void Timer_Init(void)
{
    __HAL_RCC_TIM3_CLK_ENABLE();
    /* TODO: Use TIM6? */
    /* TIM3_TRGO triggers ADC2 */
    TIM3->CR1 = TIM_CLOCKDIVISION_DIV1 | TIM_COUNTERMODE_UP | TIM_AUTORELOAD_PRELOAD_ENABLE;
    TIM3->CR2 = TIM_TRGO_UPDATE;
    TIM3->PSC = 0;
    TIM3->ARR = (2 * HAL_RCC_GetPCLK1Freq() / AUDIO_SAMPLE_RATE) - 1;
    TIM3->EGR = TIM_EGR_UG;
#if 1 /* Output sample rate on compare channel 3 */
    TIM3->CCMR2 =  TIM_OCMODE_PWM1 | TIM_CCMR2_OC3PE;
    TIM3->CCER = (0 << TIM_CCER_CC3P_Pos) | TIM_CCER_CC3E;
    TIM3->CCR3 = 500;
#endif
    TIM3->DIER = TIM_DIER_UIE;
    TIM3->CR1 |= TIM_CR1_CEN;



    __HAL_RCC_TIM2_CLK_ENABLE();

    /* TIM2 generates a timebase for USB OUT feedback endpoint */
    TIM2->CR1 = TIM_CLOCKDIVISION_DIV1 | TIM_COUNTERMODE_UP | TIM_AUTORELOAD_PRELOAD_ENABLE;
    TIM2->PSC = 0;
    TIM2->ARR = 0xFFFFFFFFUL;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->CR1 |= TIM_CR1_CEN;

}

static void ADC_Init(void)
{
    __HAL_RCC_ADC2_CLK_ENABLE();

    ADC2->CR = 0x00 << ADC_CR_ADVREGEN_Pos;
    ADC2->CR = 0x01 << ADC_CR_ADVREGEN_Pos;

    for (uint32_t i=0; i<200; i++) {
        asm volatile ("nop");
    }

    /* Select AHB clock */
    ADC12_COMMON->CCR = (0x1 << ADC12_CCR_CKMODE_Pos) | (0x00 << ADC12_CCR_MULTI_Pos);

    ADC2->CR |= ADC_CR_ADCAL;

    while (ADC2->CR & ADC_CR_ADCAL)
        ;

    ADC2->CR |= ADC_CR_ADEN;

    /* Wait for ADC to be ready */
    while (!(ADC2->ISR & ADC_ISR_ADRDY))
        ;

    /* External Trigger on TIM3_TRGO, left aligned data with 12 bit resolution */
    ADC2->CFGR = (0x01 << ADC_CFGR_EXTEN_Pos)  | (0x04 << ADC_CFGR_EXTSEL_Pos) | (ADC_CFGR_ALIGN) | (0x00 << ADC_CFGR_RES_Pos);

    /* Maximum sample time of 601.5 cycles for channel 12. */
    ADC2->SMPR2 = 0x7 << ADC_SMPR2_SMP12_Pos;

    /* Sample only channel 12 in a regular sequence */
    ADC2->SQR1 = (12 << ADC_SQR1_SQ1_Pos) | (0 << ADC_SQR1_L_Pos);

    /* Enable Interrupt Request */
    ADC2->IER = ADC_IER_EOSIE;

    /* Start ADC */
    ADC2->CR |= ADC_CR_ADSTART;


}

void DAC_Init(void)
{
    __HAL_RCC_DAC1_CLK_ENABLE();

    /* TSEL1 == TIM3 trigger */
    __HAL_REMAPTRIGGER_ENABLE(HAL_REMAPTRIGGER_DAC1_TRIG);

    DAC->CR = (0x1 << DAC_CR_TSEL1_Pos) | DAC_CR_TEN1 | DAC_CR_EN1;
}

void USB_AudioInit(void)
{
    sampFreq = AUDIO_SAMPLE_RATE;
    clkValid = 1;

    sampleFreqRng.wNumSubRanges = 1;
    sampleFreqRng.subrange[0].bMin = AUDIO_SAMPLE_RATE;
    sampleFreqRng.subrange[0].bMax = AUDIO_SAMPLE_RATE;
    sampleFreqRng.subrange[0].bRes = 0;

    GPIO_Init();
    Timer_Init();
    ADC_Init();
    DAC_Init();

}