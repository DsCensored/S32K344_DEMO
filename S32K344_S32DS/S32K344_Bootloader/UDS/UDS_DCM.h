/*
 * UDS_DCM.h
 *
 *  Generic DCM core API.  User configuration is in UDS_DcmCfg.h/.c.
 */

#ifndef UDS_DCM_H_
#define UDS_DCM_H_

#include "UDS_CanTp.h"
#include "UDS_DcmCfg.h"

void UDS_DCM_Init(void);

/*
 * addr tells the DCM whether the request was received on physical or
 * functional addressing.  Services configured as physical-only (e.g. 0x31)
 * are silently ignored on functional addressing.
 */
void UDS_DCM_RxIndication(const unsigned char *req, unsigned int len,
                          UDS_DcmAddressType addr);

/*
 * Call UDS_DCM_Poll() periodically (e.g. every 100 ms).
 *
 * Besides the session/security/reset timers it drives the NRC 0x78
 * (requestCorrectlyReceived-ResponsePending) sequence:
 *   - the first 0x78 of a request is sent immediately from
 *     UDS_DCM_RxIndication(),
 *   - on every poll the handler that returned 0x78 is executed again,
 *   - a further 0x78 is sent every UDS_DCM_RESPONSE_PENDING_DELAY_MS until
 *     the handler returns 0 (positive response) or an NRC.
 *
 * While the sequence is active, the pending request is always processed to
 * completion.  A newly received request is discarded (it is not queued) and
 * answered with NRC 0x21 (busyRepeatRequest) on physical addressing; on
 * functional addressing it is ignored, as no negative response is allowed for
 * it.
 */
void UDS_DCM_Poll(void);

UDS_DcmSessionType UDS_DCM_GetSession(void);
unsigned char UDS_DCM_IsSecurityUnlocked(void);

/* Can be overridden by the application. */
void UDS_DCM_ResetHook(void);

#endif /* UDS_DCM_H_ */
