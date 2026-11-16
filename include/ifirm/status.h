#ifndef IFIRM_STATUS_H
#define IFIRM_STATUS_H

#include <ifirm/types.h>

#define IFIRM_OK              ((ifirm_status_t)0)
#define IFIRM_ERR_NOT_FOUND   ((ifirm_status_t)-1)
#define IFIRM_ERR_INVALID_ARG ((ifirm_status_t)-2)
#define IFIRM_ERR_NO_MEMORY   ((ifirm_status_t)-3)
#define IFIRM_ERR_NO_SPACE    ((ifirm_status_t)-4)
#define IFIRM_ERR_DENIED      ((ifirm_status_t)-5)
#define IFIRM_ERR_BAD_HANDLE  ((ifirm_status_t)-6)
#define IFIRM_ERR_INVALID_STATE ((ifirm_status_t)-7)
#define IFIRM_ERR_BUSY        ((ifirm_status_t)-8)
#define IFIRM_ERR_RETRY       ((ifirm_status_t)-9)
#define IFIRM_ERR_TIMEOUT     ((ifirm_status_t)-10)
#define IFIRM_ERR_AGAIN       ((ifirm_status_t)-11)
#define IFIRM_ERR_CANCELED    ((ifirm_status_t)-12)
#define IFIRM_ERR_UNSUPPORTED ((ifirm_status_t)-13)
#define IFIRM_ERR_CORRUPT     ((ifirm_status_t)-14)
#define IFIRM_ERR_REVOKED     ((ifirm_status_t)-15)
#define IFIRM_ERR_STALE_EPOCH ((ifirm_status_t)-16)
#define IFIRM_ERR_OVERFLOW    ((ifirm_status_t)-17)
#define IFIRM_ERR_BAD_DIGEST  ((ifirm_status_t)-18)
#define IFIRM_ERR_NOT_READY   ((ifirm_status_t)-19)

const char *status_str(ifirm_status_t st);

#endif /* IFIRM_STATUS_H */
