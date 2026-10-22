#ifndef AW_LICENSE_AUTH_H
#define AW_LICENSE_AUTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
    typedef enum {
        AW_RESULT_TYPE_SUCCESS = 0,
        AW_RESULT_TYPE_ERROR = -1,
    } AwResultType;

    int aw_license_auth_gen(const char* info, uint32_t info_length, char* out_sign, size_t out_sign_buffer_size, size_t * out_size);

#ifdef __cplusplus
}
#endif // __cplusplus
#endif //AW_LICENSE_AUTH_H
