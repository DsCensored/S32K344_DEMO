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
void UDS_DCM_Poll(void);

UDS_DcmSessionType UDS_DCM_GetSession(void);
unsigned char UDS_DCM_IsSecurityUnlocked(void);

/* Can be overridden by the application. */
void UDS_DCM_ResetHook(void);

#endif /* UDS_DCM_H_ */
