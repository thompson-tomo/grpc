/* This file was generated by upb_generator from the input file:
 *
 *     xds/annotations/v3/migrate.proto
 *
 * Do not edit -- your changes will be discarded when the file is
 * regenerated.
 * NO CHECKED-IN PROTOBUF GENCODE */



#ifndef XDS_ANNOTATIONS_V3_MIGRATE_PROTO_UPB_H__UPBDEFS_H_
#define XDS_ANNOTATIONS_V3_MIGRATE_PROTO_UPB_H__UPBDEFS_H_

#include "upb/reflection/def.h"
#include "upb/reflection/internal/def_pool.h"

#include "upb/port/def.inc"

#ifdef __cplusplus
extern "C" {
#endif

extern _upb_DefPool_Init xds_annotations_v3_migrate_proto_upbdefinit;

UPB_INLINE const upb_MessageDef *xds_annotations_v3_MigrateAnnotation_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &xds_annotations_v3_migrate_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "xds.annotations.v3.MigrateAnnotation");
}

UPB_INLINE const upb_MessageDef *xds_annotations_v3_FieldMigrateAnnotation_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &xds_annotations_v3_migrate_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "xds.annotations.v3.FieldMigrateAnnotation");
}

UPB_INLINE const upb_MessageDef *xds_annotations_v3_FileMigrateAnnotation_getmsgdef(upb_DefPool *s) {
  _upb_DefPool_LoadDefInit(s, &xds_annotations_v3_migrate_proto_upbdefinit);
  return upb_DefPool_FindMessageByName(s, "xds.annotations.v3.FileMigrateAnnotation");
}


#ifdef __cplusplus
}  /* extern "C" */
#endif

#include "upb/port/undef.inc"

#endif  /* XDS_ANNOTATIONS_V3_MIGRATE_PROTO_UPB_H__UPBDEFS_H_ */
