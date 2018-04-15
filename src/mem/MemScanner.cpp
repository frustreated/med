#include <iostream>
#include <unistd.h> //getpagesize()
#include <utility>

#include "mem/MemScanner.hpp"
#include "med/MemOperator.hpp"
#include "mem/Pem.hpp"
#include "mem/MemList.hpp"

using namespace std;

const int STEP = 1;
const int CHUNK_SIZE = 128;
const int ADDRESS_SORTABLE_SIZE = 800;

MemScanner::MemScanner() {
  pid = 0;
  initialize();
}

MemScanner::MemScanner(pid_t pid) {
  this->pid = pid;
  initialize();
  memio->setPid(pid);
}

MemScanner::~MemScanner() {
  pid = 0;
  delete memio;
  delete threadManager;
  delete scope;
}

void MemScanner::initialize() {
  threadManager = new ThreadManager();
  threadManager->setMaxThreads(8);
  memio = new MemIO();
  scope = new AddressPair(0, 0);
}

void MemScanner::setPid(pid_t pid) {
  this->pid = pid;
  memio->setPid(pid);
}

pid_t MemScanner::getPid() {
  return pid;
}

MemIO* MemScanner::getMemIO() {
  return memio;
}

vector<MemPtr> MemScanner::scanInner(Byte* value,
                                     int size,
                                     Address base,
                                     int blockSize,
                                     const string& scanType,
                                     const ScanParser::OpType& op) {
  vector<MemPtr> list;
  for (Address addr = base; addr + size <= base + blockSize; addr += STEP) {
    if (memCompare((void*)addr, size, value, size, op)) {
      MemPtr mem = memio->read(addr, size);
      PemPtr pem = Pem::convertToPemPtr(mem, memio);
      pem->setScanType(scanType);
      pem->rememberValue((Byte*)addr, size);

      list.push_back(pem);
    }
  }
  return list;
}

vector<MemPtr> MemScanner::scanUnknownInner(Address base,
                                            int blockSize,
                                            const string& scanType) {
  int size = scanTypeToSize(scanType);
  vector<MemPtr> list;
  for (Address addr = base; addr + size <= base + blockSize; addr += STEP) {
    MemPtr mem = memio->read(addr, size);
    PemPtr pem = Pem::convertToPemPtr(mem, memio);
    pem->setScanType(scanType);
    pem->rememberValue((Byte*)addr, size);

    list.push_back(pem);
  }
  return list;
}

vector<MemPtr> MemScanner::filterInner(const vector<MemPtr>& list,
                                       Byte* value,
                                       int size,
                                       const string& scanType,
                                       const ScanParser::OpType& op) {
  vector<MemPtr> newList;
  for (size_t i = 0; i < list.size(); i++) {
    MemPtr mem = memio->read(list[i]->getAddress(), list[i]->getSize());

    if (memCompare(mem->getData(), size, value, size, op)) {
      PemPtr pem = Pem::convertToPemPtr(mem, memio);
      pem->setScanType(scanType);
      newList.push_back(pem);
    }
  }
  return newList;
}

vector<MemPtr> MemScanner::filterUnknownInner(const vector<MemPtr>& list,
                                              const string& scanType,
                                              const ScanParser::OpType& op) {
  int size = scanTypeToSize(scanType);
  vector<MemPtr> newList;
  for (size_t i = 0; i < list.size(); i++) {
    MemPtr mem = memio->read(list[i]->getAddress(), list[i]->getSize());
    PemPtr pem = static_pointer_cast<Pem>(list[i]);
    Byte* oldValue = pem->recallValuePtr();

    if (memCompare(mem->getData(), size, oldValue, size, op)) {
      PemPtr newPem = Pem::convertToPemPtr(mem, memio);
      newPem->setScanType(scanType);
      newPem->rememberValue(mem->getData(), size);
      newList.push_back(newPem);
    }
  }
  return newList;
}

vector<MemPtr> MemScanner::scan(Byte* value,
                                      int size,
                                      const string& scanType,
                                      const ScanParser::OpType& op) {
  if (hasScope()) {
    return scanByScope(value, size, scanType, op);
  }
  else {
    return scanByMaps(value, size, scanType, op);
  }
}

vector<MemPtr> MemScanner::scanByMaps(Byte* value,
                                      int size,
                                      const string& scanType,
                                      const ScanParser::OpType& op) {
  vector<MemPtr> list;

  Maps maps = getMaps(pid);
  int memFd = getMem(pid);
  MemIO* memio = getMemIO();

  auto& mutex = listMutex;

  for (size_t i = 0; i < maps.size(); i++) {
    TMTask* fn = new TMTask();
    *fn = [memio, &mutex, &list, &maps, i, memFd, value, size, scanType, op]() {
      scanMap(memio, mutex, list, maps, i, memFd, value, size, scanType, op);
    };
    threadManager->queueTask(fn);
  }
  threadManager->start();
  threadManager->clear();

  close(memFd);

  if (list.size() <= ADDRESS_SORTABLE_SIZE) {
    return MemList::sortByAddress(list);
  }
  return list;
}

vector<MemPtr> MemScanner::scanByScope(Byte* value,
                                       int size,
                                       const string& scanType,
                                       const ScanParser::OpType& op) {
  vector<MemPtr> list;
  auto start = scope->first;
  auto end = scope->second;
  int fd = getMem(pid);
  auto& mutex = listMutex;

  // TODO: Refactor this, since similar to scanMap()
  for (Address j = start; j < end; j += getpagesize()) {
    if (lseek(fd, j, SEEK_SET) == -1) {
      continue;
    }

    Byte* page = new Byte[getpagesize()];

    if (read(fd, page, getpagesize()) == -1) {
      continue;
    }
    scanPage(memio, mutex, list, page, j, value, size, scanType, op);

    delete[] page;
  }

  if (list.size() <= ADDRESS_SORTABLE_SIZE) {
    return MemList::sortByAddress(list);
  }
  return list;
}

vector<MemPtr>& MemScanner::saveSnapshot(const vector<MemPtr>& baseList) {
  snapshot.clear();
  if (hasScope()) {
    return saveSnapshotByScope();
  }
  else {
    return saveSnapshotByList(baseList);
  }
}

vector<MemPtr>& MemScanner::saveSnapshotByList(const vector<MemPtr>& baseList) {
  if (!baseList.size()) {
    throw EmptyListException("Should not scan unknown with empty list");
  }
  Maps allMaps = getMaps(pid);
  Maps maps = getInterestedMaps(allMaps, baseList);

  MemIO* memio = getMemIO();

  for (size_t i = 0; i < maps.size(); i++) {
    saveSnapshotMap(memio, snapshot, maps, i);
  }
  return snapshot;
}

vector<MemPtr>& MemScanner::saveSnapshotByScope() {
  int size = getpagesize();

  auto start = scope->first;
  auto end = scope->second;
  for (Address j = start; j < end; j += size) {
    try {
      MemPtr mem = memio->read(j, size);
      snapshot.push_back(mem);
    } catch(MedException& ex) {
      cerr << ex.getMessage() << endl;
    }
  }
  return snapshot;
}

void MemScanner::scanMap(MemIO* memio,
                         std::mutex& mutex,
                         vector<MemPtr>& list,
                         Maps& maps,
                         int mapIndex,
                         int fd,
                         Byte* value,
                         int size,
                         const string& scanType,
                         const ScanParser::OpType& op) {
  auto& pairs = maps.getMaps();
  auto& pair = pairs[mapIndex];
  for (Address j = std::get<0>(pair); j < std::get<1>(pair); j += getpagesize()) {
    if (lseek(fd, j, SEEK_SET) == -1) {
      continue;
    }

    Byte* page = new Byte[getpagesize()]; //For block of memory

    if (read(fd, page, getpagesize()) == -1) {
      continue;
    }
    scanPage(memio, mutex, list, page, j, value, size, scanType, op);

    delete[] page;
  }
}

void MemScanner::saveSnapshotMap(MemIO* memio,
                                 vector<MemPtr>& snapshot,
                                 Maps& maps,
                                 int mapIndex) {
  int size = getpagesize();

  auto& pairs = maps.getMaps();
  auto& pair = pairs[mapIndex];
  for (Address j = std::get<0>(pair); j < std::get<1>(pair); j += size) {
    try {
      MemPtr mem = memio->read(j, size);
      snapshot.push_back(mem);
    } catch(MedException& ex) {
      cerr << ex.getMessage() << endl;
    }
  }
}

void MemScanner::scanPage(MemIO* memio,
                          std::mutex& mutex,
                          vector<MemPtr>& list,
                          Byte* page,
                          Address start,
                          Byte* value,
                          int size,
                          const string& scanType,
                          const ScanParser::OpType& op) {
  for (int k = 0; k <= getpagesize() - size; k += STEP) {
    try {
      if (memCompare(page + k, size, value, size, op)) {
        MemPtr mem = memio->read((Address)(start + k), size);

        PemPtr pem = Pem::convertToPemPtr(mem, memio);
        pem->setScanType(scanType);
        pem->rememberValue(page + k, size);

        mutex.lock();
        list.push_back(pem);
        mutex.unlock();
      }
    } catch(MedException& ex) {
      cerr << ex.getMessage() << endl;
    }
  }
}

vector<MemPtr> MemScanner::filter(const vector<MemPtr>& list,
                                  Byte* value,
                                  int size,
                                  const string& scanType,
                                  const ScanParser::OpType& op) {
  vector<MemPtr> newList;

  auto& mutex = listMutex;

  for (size_t i = 0; i < list.size(); i += CHUNK_SIZE) {
    TMTask* fn = new TMTask();
    *fn = [&mutex, &list, &newList, i, value, size, scanType, op]() {
      filterByChunk(mutex, list, newList, i, value, size, scanType, op);
    };
    threadManager->queueTask(fn);
  }
  threadManager->start();
  threadManager->clear();

  if (newList.size() <= ADDRESS_SORTABLE_SIZE) {
    return MemList::sortByAddress(newList);
  }
  return newList;
}

vector<MemPtr> MemScanner::filterUnknown(const vector<MemPtr>& list,
                                                 const string& scanType,
                                                 const ScanParser::OpType& op) {
  if (snapshot.size()) {
    return filterSnapshot(scanType, op);
  }
  else {
    return filterUnknownWithList(list, scanType, op);
  }
}

vector<MemPtr> MemScanner::filterUnknownWithList(const vector<MemPtr>& list,
                                                 const string& scanType,
                                                 const ScanParser::OpType& op) {
  vector<MemPtr> newList;

  auto& mutex = listMutex;

  for (size_t i = 0; i < list.size(); i += CHUNK_SIZE) {
    TMTask* fn = new TMTask();
    *fn = [&mutex, &list, &newList, i, scanType, op]() {
      filterUnknownByChunk(mutex, list, newList, i, scanType, op);
    };
    threadManager->queueTask(fn);
  }
  threadManager->start();
  threadManager->clear();

  if (newList.size() <= ADDRESS_SORTABLE_SIZE) {
    return MemList::sortByAddress(newList);
  }
  return newList;
}

void MemScanner::filterByChunk(std::mutex& mutex,
                               const vector<MemPtr>& list,
                               vector<MemPtr>& newList,
                               int listIndex,
                               Byte* value,
                               int size,
                               const string& scanType,
                               const ScanParser::OpType& op) {
  for (int i = listIndex; i < listIndex + CHUNK_SIZE && i < (int)list.size(); i++) {
    PemPtr pem = static_pointer_cast<Pem>(list[i]);
    Byte* data = pem->getValuePtr();

    if (memCompare(data, size, value, size, op)) {
      pem->setScanType(scanType);
      pem->rememberValue(data, size);

      mutex.lock();
      newList.push_back(pem);
      mutex.unlock();
    }
    delete[] data;
  }
}



void MemScanner::filterUnknownByChunk(std::mutex& mutex,
                                      const vector<MemPtr>& list,
                                      vector<MemPtr>& newList,
                                      int listIndex,
                                      const string& scanType,
                                      const ScanParser::OpType& op) {
  for (int i = listIndex; i < listIndex + CHUNK_SIZE && i < (int)list.size(); i++) {
    int size = scanTypeToSize(scanType);
    PemPtr pem = static_pointer_cast<Pem>(list[i]);
    Byte* data = pem->getValuePtr();
    Byte* oldValue = pem->recallValuePtr();

    if (memCompare(data, size, oldValue, size, op)) {
      pem->setScanType(scanType);
      pem->rememberValue(data, size);

      mutex.lock();
      newList.push_back(pem);
      mutex.unlock();
    }
    delete[] data;
  }
}

Maps MemScanner::getInterestedMaps(Maps& maps, const vector<MemPtr>& list) {
  Maps interested;
  for (size_t i = 0; i < list.size(); i++) {
    for (size_t j = 0; j < maps.size(); j++) {
      auto& pairs = maps.getMaps();
      auto& pair = pairs[j];
      auto start = std::get<0>(pair);
      auto end = std::get<1>(pair);
      bool inRegion = list[i]->getAddress() >= start && list[i]->getAddress() <= end;

      if (inRegion) {
        AddressPair pair(start, end);
        if (!interested.hasPair(pair)) {
          interested.push(pair);
        }
        break;
      }
    }
  }
  return interested;
}

vector<MemPtr> MemScanner::filterSnapshot(const string& scanType, const ScanParser::OpType& op) {
  vector<MemPtr> list;
  for (size_t i = 0; i < snapshot.size(); i++) {
    auto block = memio->read(snapshot[i]->getAddress(), snapshot[i]->getSize());
    compareBlocks(list, snapshot[i], block, scanType, op);
  }
  snapshot.clear();
  return list;
}

void MemScanner::compareBlocks(vector<MemPtr>& list, MemPtr& oldBlock, MemPtr& newBlock, const string& scanType, const ScanParser::OpType& op) {
  size_t blockSize = oldBlock->getSize();
  int size = scanTypeToSize(scanType);
  Byte* oldBlockPtr = oldBlock->getData();
  Byte* newBlockPtr = newBlock->getData();
  for (size_t i = 0; i <= blockSize - size; i += STEP) {
    if (memCompare(newBlockPtr + i, size, oldBlockPtr + i, size, op)) {
      MemPtr mem = memio->read(oldBlock->getAddress() + i, size);
      PemPtr pem = Pem::convertToPemPtr(mem, memio);
      pem->setScanType(scanType);
      pem->rememberValue(mem->getData(), size);

      list.push_back(pem);
    }
  }
}

AddressPair* MemScanner::getScope() {
  return scope;
}

void MemScanner::setScopeStart(Address addr) {
  scope->first = addr;
}

void MemScanner::setScopeEnd(Address addr) {
  scope->second = addr;
}

bool MemScanner::hasScope() {
  return scope->first && scope->second;
}

std::mutex& MemScanner::getListMutex() {
  return listMutex;
}
