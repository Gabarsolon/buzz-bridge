#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void ds3_journal_init(void);
bool ds3_journal_save(uint32_t reason);
void ds3_journal_task(bool host_core_ready);
void ds3_journal_dump_start(void);
bool ds3_journal_dump_line(char *out,size_t capacity);
bool ds3_journal_dump_active(void);
