#ifndef _SPOTD_TYPES_H_
#define _SPOTD_TYPES_H_

typedef enum spotd_error {
  SPOTD_ERROR_OK              = 0, // No errors encountered
  SPOTD_ERROR_BIND_FAILED     = 1, // Server bind call failed
  SPOTD_ERROR_OTHER_PERMANENT = 2  // Some other error occurred, and it is permanent
} spotd_error;

#endif /* _SPOTD_TYPES_H_ */
