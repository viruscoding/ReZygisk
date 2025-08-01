#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#include <stdint.h>

int read_fd(int fd);

ssize_t write_string(int fd, const char *str);
  
char *read_string(int fd);

#define write_func_def(type)              \
  ssize_t write_## type(int fd, type val)

#define read_func_def(type)               \
  ssize_t read_## type(int fd, type *val)

write_func_def(uint8_t);
read_func_def(uint8_t);

write_func_def(uint32_t);
read_func_def(uint32_t);

write_func_def(size_t);
read_func_def(size_t);

#endif /* SOCKET_UTILS_H */