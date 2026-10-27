/* Syntax-only contract check against the target BSP's real ag_proto.h.
 * This checks the documented checksum-disabled header variant; it is not a
 * UART implementation, packet parser, or a substitute for the vendor library. */
#include <stdint.h>
#include <stddef.h>
#include <aglink/ag_proto.h>

#ifdef AGLINK_HDR_CKSUM
#error "This protocol document/check targets checksum=0; verify the CRC variant separately"
#endif

_Static_assert(sizeof(struct aglink_hdr) == 8, "AGLink header changed");
_Static_assert(offsetof(struct aglink_hdr, payload) == 8, "payload offset changed");
_Static_assert(offsetof(struct aglink_hdr, seq) == 4, "sequence offset changed");
_Static_assert(sizeof(struct ag_time) == 14, "ag_time layout changed");
_Static_assert(offsetof(struct ag_time, timestamp) == 1, "timestamp offset changed");
_Static_assert(sizeof(struct ag_sync) == 6, "sync payload changed");
_Static_assert(sizeof(struct ag_generic_rsp) == 2, "response payload changed");
_Static_assert(AG_MODE_PHOTO == 0 && AG_MODE_VIDEO == 1 && AG_MODE_RECORD_AUDIO == 6, "mode IDs changed");
_Static_assert(AG_AD_TIME == 1 && AG_AD_AUDIO == 5 && AG_AD_VIDEO_STOP == 7 &&
               AG_AD_RECORD_AUDIO_STOP == 8 && AG_AD_SAVE_SYS_LOG == 16, "AD IDs changed");
_Static_assert(AG_VD_SYNC_1 == 1 && AG_VD_SYNC_2 == 2 && AG_VD_THUMB == 3 &&
               AG_VD_THUMB_END == 4 && AG_VD_POWER_OFF == 5 && AG_VD_VIDEO_START == 8 &&
               AG_VD_VIDEO_END == 9 && AG_VD_RECORD_AUDIO_START == 16 &&
               AG_VD_GENERIC_RESPONSE == 21, "VD IDs changed");

_Static_assert(AG_FAILED_DISK_FULL == 15 && AG_FAILED_MOUNT_DISK == 16 &&
               AG_FAILED_UNKNOWN == 17, "error codes changed");
