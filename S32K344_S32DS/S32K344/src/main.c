/* Copyright 2023 NXP */
/* License: BSD 3-clause
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/
/*
 * main implementation: use this 'C' sample to create your own application
 *
 */
#include "S32K344.h"
#include "UserOs.h"
#include "UserCode.h"
#include "BSW_Init.h"
#include "BSW_CanCallback.h"
#include <stdio.h>

int main(void)
{
	/* Only what the first CAN frame needs: clock, Port, interrupts, the 1 ms
	 * tick and CAN/CanIf.  Everything else (NVM, ICU, watchdog, UART/DMA) is
	 * done slice by slice in BSW_InitLateTask() while the OS is already
	 * running, so the start-up does not delay the periodic frames. */
	BSW_InitEarly();

	/* CAN tables, CAN-TP/DCM, application models and the task registration
	 * (including the deferred-init task). */
	ASW_Init();

	/* First frame: ASW_CanInit() left every TX entry due, so this single poll
	 * puts the periodic frames on the bus before the OS even starts.  The
	 * period bookkeeping inside BSW_CANTX_POLL() then continues from here. */
	BSW_CAN_SendFirstFrame();

	BSW_OsStart();
}


