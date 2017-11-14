#include <iostream>
#include <cstdio>

#include "med/Process.hpp"
#include "med/Snapshot.hpp"
#include "med/MedException.hpp"
#include "med/MedCommon.hpp"
#include "med/MemOperator.hpp"

using namespace std;

Snapshot::Snapshot() {
  scanUnknown = false;
}
Snapshot::~Snapshot() {
  memoryBlocks.clear();
}

void Snapshot::save(Process* process) {
  this->process = process;

  if (process->pid.length() == 0) {
    MedException("Process ID is empty");
    return;
  }
  memoryBlocks.clear();
  memoryBlocks = process->pullMemory();

  scanUnknown = true;
}

vector<MedScan> Snapshot::compare(const ScanParser::OpType& opType, const ScanType& scanType) {
  vector<MedScan> emptyScan;
  if (process->pid.length() == 0) {
    MedException("Process ID is empty");
    return emptyScan;
  }
  if (memoryBlocks.getSize() == 0) {
    MedException("Memory blocks is empty");
    return emptyScan;
  }
  MemoryBlocks currentMemoryBlocks = process->pullMemory();
  MemoryBlockPairs pairs = createMemoryBlockPairs(memoryBlocks, currentMemoryBlocks);
  auto result = filter(pairs, opType, scanType);
  return result;
}

MemoryBlockPairs Snapshot::createMemoryBlockPairs(MemoryBlocks prev, MemoryBlocks curr) {
  MemoryBlockPairs pairs;
  vector<MemoryBlock> prevBlocks = prev.getData();
  vector<MemoryBlock> currBlocks = curr.getData();
  for (size_t i = 0; i < prevBlocks.size(); i++) {
    for (size_t j = 0; j < currBlocks.size(); j++) {
      if (isBlockMatched(prevBlocks[i], currBlocks[j])) {
        MemoryBlockPair pair(prevBlocks[i], currBlocks[j]);
        pairs.push_back(pair);
        break;
      }
    }
  }
  return pairs;
}

bool Snapshot::isBlockMatched(MemoryBlock block1, MemoryBlock block2) {
  MemAddr firstAddress1 = block1.getAddress();
  MemAddr lastAddress1 = firstAddress1 + block1.getSize();
  MemAddr firstAddress2 = block2.getAddress();
  MemAddr lastAddress2 = firstAddress2 + block2.getSize();
  return (firstAddress1 <= firstAddress2 && lastAddress1 >= firstAddress2) ||
    (firstAddress1 <= lastAddress2 && lastAddress1 >= lastAddress2);
}

vector<MedScan> Snapshot::filter(const MemoryBlockPairs& pairs, const ScanParser::OpType& opType, const ScanType& scanType) {
  vector<SnapshotScan> scan;
  for (size_t i = 0; i < pairs.size(); i++) {
    vector<SnapshotScan> found = comparePair(pairs[i], opType, scanType);

    scan.insert(scan.end(), found.begin(), found.end());
  }
  // TODO: Need to think about whether to return as MedScan
  return SnapshotScan::toMedScans(scan);
}


vector<SnapshotScan> Snapshot::comparePair(const MemoryBlockPair& pair, const ScanParser::OpType& opType, const ScanType& scanType) {
  auto first = pair.first;
  auto second = pair.second;
  int offset = first.getAddress() - second.getAddress();
  int length = first.getSize();

  int typeSize = scanTypeToSize(scanType);

  vector<SnapshotScan> scan;
  for (int i = 0; i < length; i++) {
    auto firstData = first.getData();
    auto secondData = second.getData();
    bool result = memCompare(&firstData[i], &secondData[offset + i], typeSize, opType);
    if (result) {
      scan.push_back(SnapshotScan(first.getAddress() + i));
    }
  }
  return scan;
}

bool Snapshot::isUnknown() {
  return scanUnknown;
}
