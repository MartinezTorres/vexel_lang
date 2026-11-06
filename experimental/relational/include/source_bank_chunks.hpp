#pragma once
#include <string>
#include <vector>
namespace vx::relational {
struct BankChunk {std::string name,module,signature,body;};
struct BankChunks {std::string header,body;std::vector<BankChunk> parts;};
// Partition only our closed C projection, whose control grammar is known.
// The shared frame is a target implementation detail, never a source pointer.
BankChunks splitBankFunction(const std::string& name,const std::string& signature,
                            const std::string& body,unsigned operations,bool staticFrame);
}
