#pragma once
#include <stdint.h>

typedef struct { uint16_t implementer; uint16_t part; const char *name; } midr_name_entry;
static const midr_name_entry midr_name_table[] = {};
static const unsigned midr_name_table_len = sizeof(midr_name_table)/sizeof(midr_name_table[0]);

typedef struct { uint16_t implementer; const char *vendor; } midr_vendor_entry;
static const midr_vendor_entry midr_vendor_table[] = {};
static const unsigned midr_vendor_table_len = sizeof(midr_vendor_table)/sizeof(midr_vendor_table[0]);
