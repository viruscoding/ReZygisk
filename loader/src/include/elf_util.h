#ifndef ELF_UTIL_H
#define ELF_UTIL_H

#include <string.h>
#include <link.h>
#include <linux/elf.h>
#include <sys/types.h>

#define SHT_GNU_HASH 0x6ffffff6

struct symtabs {
  char *name;
  ElfW(Sym) *sym;
};

typedef struct {
  char *elf;
  void *base;
  ElfW(Ehdr) *header;
  size_t size;
  off_t bias;
  ElfW(Shdr) *section_header;

  ElfW(Shdr) *dynsym;
  ElfW(Off) dynsym_offset;
  ElfW(Sym) *dynsym_start;
  ElfW(Shdr) *strtab;
  ElfW(Off) symstr_offset;
  void *strtab_start;

  uint32_t nbucket_;
  uint32_t *bucket_;
  uint32_t *chain_;

  uint32_t gnu_nbucket_;
  uint32_t gnu_symndx_;
  uint32_t gnu_bloom_size_;
  uint32_t gnu_shift2_;
  uintptr_t *gnu_bloom_filter_;
  uint32_t *gnu_bucket_;
  uint32_t *gnu_chain_;

  ElfW(Shdr) *symtab;
  ElfW(Off) symtab_offset;
  size_t symtab_size;
  size_t symtab_count;
  ElfW(Sym) *symtab_start;
  ElfW(Off) symstr_offset_for_symtab;

  struct symtabs *symtabs_;
} ElfImg;

void ElfImg_destroy(ElfImg *img);

ElfImg *ElfImg_create(const char *elf, void *base);

ElfW(Addr) ElfLookup(ElfImg *restrict img, const char *restrict name, uint32_t hash);

ElfW(Addr) GnuLookup(ElfImg *restrict img, const char *restrict name, uint32_t hash);

ElfW(Addr) LinearLookup(ElfImg *restrict img, const char *restrict name);

ElfW(Addr) LinearLookupByPrefix(ElfImg *restrict img, const char *name);

int dl_cb(struct dl_phdr_info *info, size_t size, void *data);

ElfW(Addr) getSymbOffset(ElfImg *img, const char *name);

ElfW(Addr) getSymbAddress(ElfImg *img, const char *name);

ElfW(Addr) getSymbAddressByPrefix(ElfImg *img, const char *prefix);

void *getSymbValueByPrefix(ElfImg *img, const char *prefix);

#endif /* ELF_UTIL_H */
