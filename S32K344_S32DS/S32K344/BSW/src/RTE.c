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

void RTE_POLLING(void)
{
	BSW_stKL15 = Dio_ReadChannel(DioConf_DioChannel_DioChannel_PB19);
}
