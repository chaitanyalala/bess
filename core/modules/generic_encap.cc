#include "../module.h"

#define MAX_FIELDS 8
#define MAX_FIELD_SIZE 8
static_assert(MAX_FIELD_SIZE <= sizeof(uint64_t),
              "field cannot be larger than 8 bytes");

#define MAX_HEADER_SIZE (MAX_FIELDS * MAX_FIELD_SIZE)

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error this code assumes little endian architecture (x86)
#endif

struct Field {
  uint64_t value; /* onlt for constant values */
  int attr_id;    /* -1 for constant values */
  int pos;        /* relative position in the new header */
  int size;       /* in bytes. 1 <= size <= MAX_FIELD_SIZE */
};

class GenericEncap : public Module {
 public:
  struct snobj *Init(struct snobj *arg);

  void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

 private:
  struct snobj *AddFieldOne(struct snobj *field, struct Field *f, int idx);

  int encap_size_;

  int num_fields_;

  struct Field fields_[MAX_FIELDS];
};

struct snobj *GenericEncap::AddFieldOne(struct snobj *field, struct Field *f,
                                        int idx) {
  if (field->type != TYPE_MAP)
    return snobj_err(EINVAL, "'fields' must be a list of maps");

  f->size = snobj_eval_uint(field, "size");
  if (f->size < 1 || f->size > MAX_FIELD_SIZE)
    return snobj_err(EINVAL, "idx %d: 'size' must be 1-%d", idx,
                     MAX_FIELD_SIZE);

  const char *attr = snobj_eval_str(field, "attr");

  struct snobj *t;

  if (attr) {
    f->attr_id = add_metadata_attr(this, attr, f->size, MT_READ);
    if (f->attr_id < 0)
      return snobj_err(-f->attr_id, "idx %d: add_metadata_attr() failed", idx);
  } else if ((t = snobj_eval(field, "value"))) {
    f->attr_id = -1;
    if (snobj_binvalue_get(t, f->size, &f->value, 1))
      return snobj_err(EINVAL,
                       "idx %d: "
                       "not a correct %d-byte value",
                       idx, f->size);
  } else
    return snobj_err(EINVAL, "idx %d: must specify 'value' or 'attr'", idx);

  return NULL;
}

/* Takes a list of fields. Each field is either:
 *
 *  1. {'size': X, 'value': Y}		(for constant values)
 *  2. {'size': X, 'attr': Y}		(for metadata attributes)
 *
 * e.g.: GenericEncap([{'size': 4, 'value':0xdeadbeef},
 *                     {'size': 2, 'attr':'foo'},
 *                     {'size': 2, 'value':0x1234}])
 * will prepend a 8-byte header:
 *    de ad be ef <xx> <xx> 12 34
 * where the 2-byte <xx> <xx> comes from the value of metadata arribute 'foo'
 * for each packet.
 */
struct snobj *GenericEncap::Init(struct snobj *arg) {
  int size_acc = 0;

  struct snobj *fields = snobj_eval(arg, "fields");

  if (snobj_type(fields) != TYPE_LIST)
    return snobj_err(EINVAL, "'fields' must be a list of maps");

  for (size_t i = 0; i < fields->size; i++) {
    struct snobj *field = snobj_list_get(fields, i);
    struct snobj *err;
    struct Field *f = &fields_[i];

    f->pos = size_acc;

    err = AddFieldOne(field, f, i);
    if (err) return err;

    size_acc += f->size;
  }

  encap_size_ = size_acc;
  num_fields_ = fields->size;

  return NULL;
}

void GenericEncap::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  int encap_size = encap_size_;

  char headers[MAX_PKT_BURST][MAX_HEADER_SIZE] __ymm_aligned;

  for (int i = 0; i < num_fields_; i++) {
    uint64_t value = fields_[i].value;

    int attr_id = fields_[i].attr_id;
    int offset = (attr_id >= 0) ? GenericEncap::attr_offsets[attr_id] : 0;

    char *header = headers[0] + fields_[i].pos;

    for (int j = 0; j < cnt; j++, header += MAX_HEADER_SIZE) {
      struct snbuf *pkt = batch->pkts[j];

      *(uint64_t *)header =
          (attr_id < 0) ? value : get_attr_with_offset(offset, pkt, uint64_t);
    }
  }

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];

    char *p = static_cast<char *>(snb_prepend(pkt, encap_size));

    if (unlikely(!p)) continue;

    rte_memcpy(p, headers[i], encap_size);
  }

  run_next_module(this, batch);
}

ADD_MODULE(GenericEncap, "generic_encap",
           "encapsulates packets with constant values and metadata attributes")
