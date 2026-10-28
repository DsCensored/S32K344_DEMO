/*
 * UDS_DcmCfg.h
 *
 *  DCM user configuration.
 *
 *  All service handlers, service/sub-function tables, routine tables and
 *  security algorithms live in UDS_DcmCfg.c.  The DCM core only performs
 *  generic dispatch and timing.
 */

#ifndef UDS_DCMCFG_H_
#define UDS_DCMCFG_H_

#define UDS_DCM_SESSION_TIMEOUT_MS      30000U
#define UDS_DCM_SECURITY_DELAY_MS       10000U
#define UDS_DCM_SECURITY_MAX_ATTEMPTS   3U
#define UDS_DCM_RESET_DELAY_MS          100U
#define UDS_DCM_NO_SUBFN                0xFFU

/* Internal pseudo NRC: the handler already took care of the response (e.g. it`r`n * resets the ECU) and the DCM must not send anything. */
#define UDS_DCM_NRC_NO_RESPONSE         0xFEU

/*
 * NRC 0x78 (requestCorrectlyReceived-ResponsePending) handling.
 *
 * When a service handler returns NRC 0x78, the DCM
 *   - sends the first 0x78 immediately,
 *   - calls the same handler again on every UDS_DCM_Poll(),
 *   - repeats 0x78 on the bus only after UDS_DCM_RESPONSE_PENDING_DELAY_MS
 *     (TBD: value from the diagnostic specification),
 *   - sends the final response as soon as the handler returns 0 or another
 *     NRC.
 *
 * UDS_DCM_PENDING_REQ_MAX_LEN is the maximum request length that is kept
 * while a response is pending.  A longer request cannot be replayed, so a
 * handler returning 0x78 for it is answered with NRC 0x10 (generalReject).
 */
#define UDS_DCM_RESPONSE_PENDING_DELAY_MS  2000U
#define UDS_DCM_PENDING_REQ_MAX_LEN        64U

typedef enum
{
    UDS_DCM_SESSION_DEFAULT     = 0x01U,
    UDS_DCM_SESSION_PROGRAMMING = 0x02U,
    UDS_DCM_SESSION_EXTENDED    = 0x03U
} UDS_DcmSessionType;

/*
 * Session bit masks for UDS_DcmServiceType.SessionMask.
 *
 * These are bit positions, not the 0x01/0x02/0x03 values that are sent on
 * the bus: a service may be available in more than one session.
 */
#define UDS_DCM_SESSION_MASK_DEFAULT     0x01U
#define UDS_DCM_SESSION_MASK_PROGRAMMING 0x02U
#define UDS_DCM_SESSION_MASK_EXTENDED    0x04U
#define UDS_DCM_SESSION_MASK_ALL         (UDS_DCM_SESSION_MASK_DEFAULT | \
                                          UDS_DCM_SESSION_MASK_PROGRAMMING | \
                                          UDS_DCM_SESSION_MASK_EXTENDED)

/*
 * Security levels for UDS_DcmServiceType.SecurityLevel.
 *
 * UDS_DcmCtx.SecurityUnlocked is a bit mask of unlocked levels, so the same
 * bits are used here.  UDS_DCM_SECURITY_NONE means "no security required".
 */
#define UDS_DCM_SECURITY_NONE   0x00U
#define UDS_DCM_SECURITY_LEVEL1 0x01U
#define UDS_DCM_SECURITY_LEVEL2 0x02U

/*
 * Addressing mode of a received request.
 *
 * UDS_DCM_ADDR_MASK_xxx are the corresponding bits for
 * UDS_DcmServiceType.AddrMask.  A service that does not have the
 * FUNCTIONAL bit set is silently ignored when it is received on functional
 * addressing (e.g. RoutineControl 0x31).
 */
typedef enum
{
    UDS_DCM_ADDRESS_PHYSICAL   = 0x01U,
    UDS_DCM_ADDRESS_FUNCTIONAL = 0x02U
} UDS_DcmAddressType;

#define UDS_DCM_ADDR_MASK_PHYSICAL   0x01U
#define UDS_DCM_ADDR_MASK_FUNCTIONAL 0x02U
#define UDS_DCM_ADDR_MASK_ALL        (UDS_DCM_ADDR_MASK_PHYSICAL | \
                                      UDS_DCM_ADDR_MASK_FUNCTIONAL)

/*
 * A handler returns 0 for a positive response, or an NRC value.
 * It fills UDS_TxBuf[] and reports the complete response length in rspLen.
 * UDS_TxBuf[0] is filled by the DCM core with SID + 0x40.
 *
 * A handler that needs more time than P2_server returns NRC 0x78
 * (requestCorrectlyReceived-ResponsePending).  The DCM stores a copy of the
 * request and calls the handler again from UDS_DCM_Poll() until it returns
 * something else; see UDS_DCM_RESPONSE_PENDING_DELAY_MS.
 */
typedef unsigned char (*UDS_DcmHandlerType)(const unsigned char *req,
                                            unsigned int len,
                                            unsigned int *rspLen);

/*
 * Request of a handler that currently answers with NRC 0x78.  It is kept in
 * RAM because the original request buffer is released once the first
 * indication has been processed.
 */
typedef struct
{
    UDS_DcmHandlerType Handler;    /* handler to call again */
    unsigned int ReqLen;           /* length of the stored request */
    unsigned int Tick;             /* tick when the last 0x78 was sent */
    unsigned char Req[UDS_DCM_PENDING_REQ_MAX_LEN];
    unsigned char SID;             /* SID of the pending request */
    unsigned char Addr;            /* UDS_DCM_ADDRESS_xxx of the request */
    unsigned char SuppressPosRsp;  /* suppressPosRsp of the request */
    unsigned char Active;          /* 1 while a 0x78 sequence is running */
} UDS_DcmPendingType;

/*
 * DCM runtime state.  It is defined in UDS_DcmCfg.c so the user can add
 * application-specific state without touching the DCM core.
 */
typedef struct
{
    UDS_DcmSessionType Session;
    unsigned int SessionTick;

    unsigned int Seed;
    unsigned char SeedLevel;
    unsigned char SecurityUnlocked;
    unsigned char SecurityFailCount;
    unsigned char SecurityDelayActive;
    unsigned int SecurityDelayTick;

    unsigned char ResetPending;
    unsigned int ResetTick;

    UDS_DcmPendingType Pending;
} UDS_DcmContextType;

typedef struct
{
    unsigned char SID;
    unsigned char SubFn;          /* UDS_DCM_NO_SUBFN if the service has none */
    unsigned char SessionMask;    /* UDS_DCM_SESSION_MASK_xxx bits */
    unsigned char SecurityLevel;  /* UDS_DCM_SECURITY_xxx bits, 0 = none */
    unsigned char AddrMask;       /* UDS_DCM_ADDR_MASK_xxx bits */
    UDS_DcmHandlerType Handler;
} UDS_DcmServiceType;

typedef struct UDS_DcmDid UDS_DcmDidType;

typedef unsigned char (*UDS_DcmDidReadType)(const UDS_DcmDidType *did,
                                            unsigned char *buf,
                                            unsigned int maxLen,
                                            unsigned int *outLen);

typedef unsigned char (*UDS_DcmDidWriteType)(const UDS_DcmDidType *did,
                                             const unsigned char *buf,
                                             unsigned int len);

struct UDS_DcmDid
{
    unsigned short Did;
    UDS_DcmDidReadType Read;
    UDS_DcmDidWriteType Write;
};

typedef unsigned char (*UDS_DcmRoutineHandlerType)(const unsigned char *req,
                                                   unsigned int len,
                                                   unsigned int *status);

typedef struct
{
    unsigned short RoutineId;
    UDS_DcmRoutineHandlerType Start;
    UDS_DcmRoutineHandlerType Stop;
    UDS_DcmRoutineHandlerType Result;
} UDS_DcmRoutineType;

extern UDS_DcmContextType UDS_DcmCtx;

extern const UDS_DcmServiceType UDS_DcmServices[];
extern const unsigned int UDS_DcmServiceCount;

extern const UDS_DcmRoutineType UDS_DcmRoutines[];
extern const unsigned int UDS_DcmRoutineCount;

extern const UDS_DcmDidType UDS_DcmDids[];
extern const unsigned int UDS_DcmDidCount;

void UDS_DcmCfgInit(void);
void UDS_DcmCfgPoll(void);
void UDS_DcmCfgSessionTimeout(void);

#endif /* UDS_DCMCFG_H_ */



