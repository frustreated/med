#ifndef MEM_H
#define MEM_H

#include <memory>
#include "med/MedTypes.hpp"

class Mem {
public:
  Mem(size_t size);
  Mem(Address addr, size_t size);
  ~Mem();

  void dump();

  Byte* data;
  size_t size;
private:
  void initialize(size_t size);
};

typedef std::shared_ptr<Mem> MemPtr;

#endif
