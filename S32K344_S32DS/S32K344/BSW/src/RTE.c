/*
 * RTE.c
 *
 *  Created on: 2026年11月2日
 *      Author: CENSORED
 */


#include "rtwtypes.h"
#include "Dio.h"
#include "Dio_Cfg.h"

uint8_T BSW_stKL15 = 0;

uint8_T ASW_stLEDR1Ctrl = 0;
uint8_T ASW_stLEDG1Ctrl = 0;
uint8_T ASW_stLEDB1Ctrl = 0;
uint8_T ASW_stLEDY1Ctrl = 0;

typedef struct
{
	const Dio_ChannelType DioChannel;
	uint8_T * const Res;
}GPIO_STU;

static GPIO_STU GPIO_IN_RTE[] =
{
		{DioConf_DioChannel_DioChannel_PB19, &BSW_stKL15},
};

static GPIO_STU GPIO_OUT_RTE[] =
{
		{DioConf_DioChannel_DioChannel_PA29, &ASW_stLEDR1Ctrl},
		{DioConf_DioChannel_DioChannel_PA30, &ASW_stLEDG1Ctrl},
		{DioConf_DioChannel_DioChannel_PA31, &ASW_stLEDB1Ctrl},
		{DioConf_DioChannel_DioChannel_PB18, &ASW_stLEDY1Ctrl},
};

static void RTE_IN_POLL(void)
{
	for(unsigned int i = 0; i < (sizeof(GPIO_IN_RTE)/sizeof(GPIO_STU)); i++)
	{
		*GPIO_IN_RTE[i].Res = Dio_ReadChannel(GPIO_IN_RTE[i].DioChannel);
	}
}

static void RTE_OUT_POLL(void)
{
	for(unsigned int i = 0; i < (sizeof(GPIO_OUT_RTE)/sizeof(GPIO_STU)); i++)
	{
		Dio_WriteChannel(GPIO_OUT_RTE[i].DioChannel,(Dio_LevelType)(*GPIO_OUT_RTE[i].Res));
	}
}

void RTE_POLLING(void)
{
	RTE_IN_POLL();
	RTE_OUT_POLL();
}
