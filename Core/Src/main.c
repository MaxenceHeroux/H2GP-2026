/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "quadspi.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "log.h" //LOG
#include <math.h>

#include "PDMS_I2C.h" //Pression


#include "W25N01G.h" //FLASH


#include "sx1262.h" //LORA


/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

//--------------------------------------GENERAL STATUS-------------------------------------------
#define XXXRACE //XXXRACE = CC

//--------------------------------------GENERAL TIMING-------------------------------------------
#ifdef RACE
	//RACE no CC
	#define EV_OPEN_TIME 100
	#define EV_CC_WAIT 5000
	#define CC_EV_WAIT 5000
	#define CC_CLOSE_TIME 25
#else
	//REGENERATION HORIZON'S TIMING
	#define EV_OPEN_TIME 500
	#define EV_CC_WAIT 5000
	#define CC_EV_WAIT 5000
	#define CC_CLOSE_TIME 100
#endif

#define TEMP_FAN_CRITIQUE  50.0f
#define TEMP_FAN_OK 35.0f
#define AVERAGE_FAN_SPEED 80
#define MAX_FAN_SPEED 100


//--------------------------------------------PIN STATE------------------------------------------------
#define EV_ON()   HAL_GPIO_WritePin(EV_GPIO_Port, EV_Pin, GPIO_PIN_RESET)  // tire à 0
#define EV_OFF()  HAL_GPIO_WritePin(EV_GPIO_Port, EV_Pin, GPIO_PIN_SET)    // relâche

#define H30_ESC_ON()  HAL_GPIO_WritePin(H30_ESC_GPIO_Port, H30_ESC_Pin, GPIO_PIN_SET)
#define H30_ESC_OFF()  HAL_GPIO_WritePin(H30_ESC_GPIO_Port, H30_ESC_Pin, GPIO_PIN_RESET)

#define CC_ON()  HAL_GPIO_WritePin(CC_GPIO_Port, CC_Pin, GPIO_PIN_SET)
#define CC_OFF()  HAL_GPIO_WritePin(CC_GPIO_Port, CC_Pin, GPIO_PIN_RESET)

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
W25N_HandleTypeDef w25n_dev;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

uint16_t ADC_ReadChannel(ADC_HandleTypeDef *hadc, uint32_t channel){
	ADC_ChannelConfTypeDef sConfig = {0};
	sConfig.Channel = channel;
	sConfig.Rank = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = ADC_SAMPLETIME_47CYCLES_5;
	sConfig.SingleDiff = ADC_SINGLE_ENDED;
	sConfig.OffsetNumber = ADC_OFFSET_NONE;
	sConfig.Offset = 0;

	if (HAL_ADC_ConfigChannel(hadc, &sConfig) != HAL_OK) {
		LOG_ERROR("ConfigChannel FAIL ch=%lu", channel);
	}

	if (HAL_ADC_Start(hadc) != HAL_OK) {
		LOG_ERROR("ADC_Start FAIL ch=%lu", channel);
	}

	HAL_StatusTypeDef poll = HAL_ADC_PollForConversion(hadc, 100);
	if (poll != HAL_OK) {
		LOG_ERROR("PollForConversion FAIL/TIMEOUT ch=%lu status=%d", channel, poll);
	}

	uint16_t data = HAL_ADC_GetValue(hadc);
	HAL_ADC_Stop(hadc);
	return data;
}

void ADC_Init(){
	if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
	  {
	      LOG_ERROR("ADC1 calibration FAILED");
	      Error_Handler();
	  }
	if (HAL_ADCEx_Calibration_Start(&hadc3, ADC_SINGLE_ENDED) != HAL_OK)
	  {
		  LOG_ERROR("ADC3 calibration FAILED");
		  Error_Handler();
	  }
}




void I2C_Scan(void){
    LOG("---- I2C SCAN START ----");
    uint8_t found = 0;

    for (uint8_t addr = 1; addr < 128; addr++){
        // HAL attend l'adresse déjà décalée (bit 0 = R/W), donc <<1
        HAL_StatusTypeDef res = HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(addr << 1), 2, 10);

        if (res == HAL_OK){
            LOG_INFO("I2C device found at 0x%02X", addr);
            found++;
        }
    }

    if (found == 0){
        LOG_WARN("No I2C device found");
    } else {
        LOG_INFO("I2C scan done, %u device(s) found", found);
    }
    LOG("---- I2C SCAN END ----");
}



void PWM_SetFreq(TIM_HandleTypeDef *htim, uint32_t channel, uint32_t freq){
    uint32_t psc = 79; // timer à 1 MHz
    uint32_t arr = (1000000 / freq) - 1;

    __HAL_TIM_SET_PRESCALER(htim, psc);
    __HAL_TIM_SET_AUTORELOAD(htim, arr);
    __HAL_TIM_SET_COMPARE(htim, channel, arr / 2);

    htim->Instance->EGR = TIM_EGR_UG;
}




void PWM_SetDuty(TIM_HandleTypeDef *htim,uint32_t channel, uint8_t duty_percent){
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(htim);

    __HAL_TIM_SET_COMPARE( htim, channel, (arr * duty_percent) / 100);
}

void FanA_SetSpeed(uint8_t speed){
    PWM_SetDuty(&htim8, TIM_CHANNEL_2, 0); // AIN1 PWM
    PWM_SetDuty(&htim3, TIM_CHANNEL_4, speed);     // AIN2 = 0
}

void FanB_SetSpeed(uint8_t speed){
    PWM_SetDuty(&htim15, TIM_CHANNEL_1, speed); // BIN1 PWM
    PWM_SetDuty(&htim15, TIM_CHANNEL_2, 0);     // BIN2 = 0
}

void FAN_Init(void){
	HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_2);   // AIN1
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);   // AIN2
	HAL_TIM_PWM_Start(&htim15, TIM_CHANNEL_1);  // BIN1
	HAL_TIM_PWM_Start(&htim15, TIM_CHANNEL_2);  // BIN2


	PWM_SetFreq(&htim8,  TIM_CHANNEL_2, 25000);
	PWM_SetFreq(&htim3,  TIM_CHANNEL_4, 25000);
	PWM_SetFreq(&htim15, TIM_CHANNEL_1, 25000);
	PWM_SetFreq(&htim15, TIM_CHANNEL_2, 25000);
}




void RGB_Init(void){
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);

    PWM_SetFreq(&htim2, TIM_CHANNEL_1, 1000); //1KHz
    PWM_SetFreq(&htim2, TIM_CHANNEL_3, 1000);
    PWM_SetFreq(&htim2, TIM_CHANNEL_4, 1000);

    //BLINK
    RGB_Set(255,0,0); //rouge
    HAL_Delay(200);
    RGB_Set(0,255,0); //vert
    HAL_Delay(200);
    RGB_Set(0,0,255); //Bleu
    HAL_Delay(200);
    RGB_Set(255,60,0); //Orange
    HAL_Delay(200);
    RGB_Set(0,0,0);
}

void RGB_Set(uint8_t r, uint8_t g, uint8_t b){

	if (r == 0 && g == 0 && b == 0)
	{
		HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
		HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_3);
		HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_4);
		return;
	}

	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_3);
	HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_4);

    // COMMON ANODE
	uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim2);

	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, arr - ((uint32_t)arr * r) / 255);
	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, arr - ((uint32_t)arr * g) / 255);
	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, arr - ((uint32_t)arr * b) / 255);

	// COMMON CATHODE
//	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, ((uint32_t)arr * r) / 255);
//	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, ((uint32_t)arr * g) / 255);
//	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_4, ((uint32_t)arr * b) / 255);
}




void Board_Init(){
	EV_OFF();
	CC_OFF();
	H30_ESC_ON();
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == GPIO_PIN_13)//EXTI
    {
        SX1262_OnDio1Irq();
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USB_DEVICE_Init();
  MX_ADC1_Init();
  MX_ADC3_Init();
  MX_I2C1_Init();
  MX_QUADSPI_Init();
  MX_SPI1_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_UART4_Init();
  MX_USART3_UART_Init();
  MX_UART5_Init();
  MX_TIM8_Init();
  MX_TIM15_Init();
  /* USER CODE BEGIN 2 */


  LOG("==== TEST W25N01G JEDEC ID ====");

  W25N_StatusTypeDef w25n_st = W25N_Init(&w25n_dev, &hqspi);

  if (w25n_st == W25N_OK) {
      LOG_INFO("W25N01G init OK - JEDEC ID match");
  } else {
      LOG_ERROR("W25N01G init FAILED, code=%d", w25n_st);

      // Diagnostic supplémentaire : lis le JEDEC ID brut même si le check a échoué
      uint8_t mfr = 0;
      uint16_t devid = 0;
      W25N_StatusTypeDef id_st = W25N_ReadJedecId(&w25n_dev, &mfr, &devid);
      if (id_st == W25N_OK) {
          LOG_INFO("RAW JEDEC: Manufacturer=0x%02X DeviceID=0x%04X", mfr, devid);
      } else {
          LOG_ERROR("ReadJedecId call FAILED, code=%d", id_st);
      }
  }
  LOG("==== FIN TEST ====");

  //SX1262 Init
  if (SX1262_InitDefault() != SX1262_OK){
	  LOG_ERROR("Init SX1262_OK");
  }

  //ADC Init
  ADC_Init();

  //PDMS Init
  WE_pdmsI2cInit();

  //Board Init
  Board_Init();

  uint32_t t_ev = 0;
  uint32_t t_cc = 0;
  uint32_t t_end = 0;
  uint32_t t_temp =0;
  uint8_t state = 0;

  uint8_t Fan_warn = 0;

  float presskPa;
  float tempDegC;


  //Start sound
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3);
  PWM_SetFreq(&htim1,TIM_CHANNEL_3, 523);  // note Do
  HAL_Delay(300);
  PWM_SetFreq(&htim1,TIM_CHANNEL_3, 440);  // note La
  HAL_Delay(300);
  PWM_SetFreq(&htim1,TIM_CHANNEL_3, 659);  // note Mi
  HAL_Delay(300);
  HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);

  //Fans
  FAN_Init();
  FanA_SetSpeed(AVERAGE_FAN_SPEED);
  FanB_SetSpeed(AVERAGE_FAN_SPEED);

  //LED
  RGB_Init();

  //I2C
  I2C_Scan();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

	  uint32_t now = HAL_GetTick();

	  /* ---------------- EV ON / OFF ---------------- */
	  if (state == 0) {
	      LOG_INFO("EV ON");
	      EV_ON();
	      t_ev = now;
	      state = 1;
	  }

	  if (state == 1 && (now - t_ev) > EV_OPEN_TIME) {
	      LOG_INFO("EV OFF");
	      EV_OFF();
	      t_ev = now;
	      state = 2;
	  }

	  /* attente 5s */
	  if (state == 2 && (now - t_ev) > EV_CC_WAIT) {
	      state = 3;
	  }

	  /* ---------------- H30 / CC pulse ---------------- */
	  if (state == 3) {

		#ifndef RACE
			LOG_INFO("H30->ESC OFF");
			H30_ESC_OFF();

			LOG_INFO("CC ON");
			CC_ON();
		#else
			LOG_INFO("RACE MODE : CC skipped");
		#endif

		t_cc = now;
		state = 4;
	  }

	  if (state == 4 && (now - t_cc) > CC_CLOSE_TIME) {
		#ifndef RACE
			LOG_INFO("CC OFF");
			CC_OFF();

			LOG_INFO("H30->ESC ON");
			H30_ESC_ON();
		#endif

		t_end = now;
		state = 5;
	  }

	  /* attente 5s */
	  if (state == 5 && (now - t_end) > CC_EV_WAIT) {
	      state = 0;
	  }

	  //Temperature Lecture
	  uint16_t adc_in1 = ADC_ReadChannel(&hadc1,ADC_CHANNEL_1);
	  uint16_t adc_in2 = ADC_ReadChannel(&hadc1, ADC_CHANNEL_2);
	  float Temp_1 = 1.0f / ((1.0f/298.15f) + (1.0f/3950.0f) * logf((adc_in1/(4095.0f-adc_in1)))) - 273.15f;
	  float Temp_2 = 1.0f / ((1.0f/298.15f) + (1.0f/3950.0f) * logf((adc_in2/(4095.0f-adc_in2)))) - 273.15f;

	  //Tension XT60
	  uint16_t adc_in3 = ADC_ReadChannel(&hadc3, ADC_CHANNEL_4);
	  uint32_t v_pc3_mv = (3300UL * adc_in3) / 4095UL;
	  uint32_t v_source_mv = (v_pc3_mv * (100UL + 20UL)) / 20UL;


	  //FAN warning
	  if (Temp_1>TEMP_FAN_CRITIQUE || Temp_2 >TEMP_FAN_CRITIQUE){
		  if(Temp_1 >50.0f)	LOG_WARN("Temperature 1 > 50 degres");
		  if(Temp_2 >50.0f)	LOG_WARN("Temperature 2 > 50 degres");
		  Fan_warn =1;
		  FanA_SetSpeed(MAX_FAN_SPEED);
		  FanB_SetSpeed(MAX_FAN_SPEED);

	  }else if (Fan_warn==1 && Temp_1<TEMP_FAN_OK && Temp_2<TEMP_FAN_OK){
		  FanA_SetSpeed(AVERAGE_FAN_SPEED);
		  FanB_SetSpeed(AVERAGE_FAN_SPEED);
		  Fan_warn =0;
	  }

/*
	  LOG_INFO("Temp_1 %ld.%ld C",((int32_t)(Temp_1 * 10.0f)) / 10,labs(((int32_t)(Temp_1 * 10.0f)) % 10));
	  LOG_INFO("Temp_2 %ld.%ld C",((int32_t)(Temp_2 * 10.0f)) / 10,labs(((int32_t)(Temp_2 * 10.0f)) % 10));
	  LOG_INFO("Tension %lu mV", v_source_mv);
	  LOG_INFO("Pression = %.2f uPa", presskPa);
	  LOG_INFO("Temp_tube = %.2f C", tempDegC); //TODO signe -
*/

	  if ((HAL_GetTick() - t_temp) >= 50){
	  		  t_temp = HAL_GetTick();

	  		  //Pression PDMS
	  		  Get_pdms(&presskPa, &tempDegC);

	  		  //LOG_INFO("NTC_VERT %lu mV", (3300UL * adc_in1) / 4095UL);
	  		  LOG_INFO("Temp_1 %ld.%ld C",((int32_t)(Temp_1 * 10.0f)) / 10,labs(((int32_t)(Temp_1 * 10.0f)) % 10));
	  		  //LOG_INFO("NTC_BLANC %lu mV", (3300UL * adc_in2) / 4095UL);
	  		  LOG_INFO("Temp_2 %ld.%ld C",((int32_t)(Temp_2 * 10.0f)) / 10,labs(((int32_t)(Temp_2 * 10.0f)) % 10));

	  		  LOG_INFO("Tension %lu mV", v_source_mv);

	  		  LOG_INFO("Pression = %.2f uPa", presskPa);
	  	  	  LOG_INFO("Temp_tube = %.2f C", tempDegC);


	  	  }

	  SX1262_Result_t ret = SX1262_TransmitText64("Bonjour depuis STM32L476", 5000);

	  if (ret == SX1262_OK){
	      LOG_INFO("Transmit TX recu");
	  }else{
	      LOG_ERROR("Erreur Transmit code=%d", ret);
	  }

	  HAL_Delay(11);

	  ret = SX1262_TransmitText64("A", 200);
	  if (ret != SX1262_OK){
	      LOG_ERROR("Erreur Transmit A code=%d", ret);
	  }

	  HAL_Delay(11);


	  char payload[64];
	  snprintf(payload, sizeof(payload), "T1:%d", 10);

	  ret = SX1262_TransmitText64(payload, 200);
	  if (ret != SX1262_OK){
	      LOG_ERROR("Erreur Transmit payload code=%d", ret);
	  }

	  HAL_Delay(11);


	  ret = SX1262_TransmitText64("C", 200);
	  if (ret != SX1262_OK){
	      LOG_ERROR("Erreur Transmit C code=%d", ret);
	  }

	  HAL_Delay(11);

  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB|RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCCLKSOURCE_PLLSAI1;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLLSAI1;
  PeriphClkInit.PLLSAI1.PLLSAI1Source = RCC_PLLSOURCE_HSE;
  PeriphClkInit.PLLSAI1.PLLSAI1M = 1;
  PeriphClkInit.PLLSAI1.PLLSAI1N = 12;
  PeriphClkInit.PLLSAI1.PLLSAI1P = RCC_PLLP_DIV7;
  PeriphClkInit.PLLSAI1.PLLSAI1Q = RCC_PLLQ_DIV4;
  PeriphClkInit.PLLSAI1.PLLSAI1R = RCC_PLLR_DIV4;
  PeriphClkInit.PLLSAI1.PLLSAI1ClockOut = RCC_PLLSAI1_48M2CLK|RCC_PLLSAI1_ADC1CLK;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
