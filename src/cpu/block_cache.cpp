#include "lucid/cpu/block_cache.hpp"
#include "lucid/cpu/cpu_memory.hpp"
#include <array>
#include <limits>
#include <utility>
namespace lucid::cpu {
namespace {
std::uint32_t load_u32_le(const std::array<std::uint8_t,4U>& b) noexcept {
    return static_cast<std::uint32_t>(b[0]) |
        (static_cast<std::uint32_t>(b[1])<<8U) |
        (static_cast<std::uint32_t>(b[2])<<16U) |
        (static_cast<std::uint32_t>(b[3])<<24U);
}
}
bool A64BlockCache::terminates_block(const std::uint32_t ins) noexcept {
    if ((ins&0x7C000000U)==0x14000000U) return true;
    if ((ins&0xFF000010U)==0x54000000U) return true;
    if ((ins&0x7E000000U)==0x34000000U) return true;
    if ((ins&0x7E000000U)==0x36000000U) return true;
    const auto branch=ins&0xFFFFFC1FU;
    if(branch==0xD61F0000U||branch==0xD63F0000U||branch==0xD65F0000U) return true;
    if(ins==0xD69F03E0U) return true;
    if((ins&0xFFE0001FU)==0xD4000001U) return true;
    if((ins&0xFFE0001FU)==0xD4200000U) return true;
    return false;
}
A64BlockCache::BasicBlock* A64BlockCache::find_or_compile(
    const std::uint64_t pc,core::MemoryBus& memory,const std::uint64_t generation){
    const auto old=blocks_.find(pc);
    if(old!=blocks_.end()){
        if(old->second.memory_generation==generation){++stats_.cache_hits;return &old->second;}
        ++stats_.invalidated_blocks;blocks_.erase(old);
    }
    ++stats_.cache_misses;
    BasicBlock block{};block.start_pc=pc;block.memory_generation=generation;
    block.instructions.reserve(MaxBasicBlockInstructions);
    CpuMemory cpu_memory(memory);
    for(std::size_t i=0;i<MaxBasicBlockInstructions;++i){
        if(i>(std::numeric_limits<std::uint64_t>::max()-pc)/4U)break;
        const auto address=pc+static_cast<std::uint64_t>(i)*4U;
        if(memory.region_generation(address)!=generation)break;
        std::array<std::uint8_t,4U> bytes{};
        if(!cpu_memory.read_instruction(address,bytes))break;
        const auto instruction=load_u32_le(bytes);
        block.instructions.push_back(instruction);
        if(terminates_block(instruction))break;
    }
    if(block.instructions.empty())return nullptr;
    ++stats_.blocks_compiled;
    auto [it,inserted]=blocks_.insert_or_assign(pc,std::move(block));
    (void)inserted;
    return &it->second;
}
A64BlockRunReport A64BlockCache::run(CpuContext& context,core::MemoryBus& memory,
                                     const std::uint64_t budget){
    A64BlockRunReport report{};
    if((context.program_counter&3U)!=0U){report.stop_reason=StepResult::MisalignedProgramCounter;return report;}
    if(budget==0U){report.instruction_budget_exhausted=true;return report;}
    CpuMemory cpu_memory(memory);
    while(report.instructions_executed<budget){
        const auto block_pc=context.program_counter;
        const auto generation=memory.region_generation(block_pc);
        if(generation==0U){report.stop_reason=StepResult::MemoryFault;break;}
        BasicBlock* block=find_or_compile(block_pc,memory,generation);
        if(block==nullptr){report.stop_reason=StepResult::MemoryFault;break;}
        bool advanced=false;
        for(std::size_t i=0;i<block->instructions.size()&&report.instructions_executed<budget;++i){
            const auto expected=block->start_pc+static_cast<std::uint64_t>(i)*4U;
            if(context.program_counter!=expected)break;
            const auto before=context.instructions_retired;
            const auto result=interpreter_.step_decoded(context,cpu_memory,block->instructions[i]);
            const auto retired=context.instructions_retired-before;
            report.instructions_executed+=retired;stats_.instructions_executed+=retired;
            report.stop_reason=result;advanced=retired!=0U;
            if(result!=StepResult::Executed){
                if(result==StepResult::Branch||result==StepResult::ExceptionTaken)break;
                return report;
            }
            if(memory.region_generation(block->start_pc)!=block->memory_generation)break;
            if(context.program_counter!=expected+4U)break;
        }
        if(!advanced)break;
    }
    report.instruction_budget_exhausted=report.instructions_executed>=budget;
    return report;
}
const A64BlockCacheStats& A64BlockCache::stats() const noexcept{return stats_;}
void A64BlockCache::clear() noexcept{blocks_.clear();stats_={};}
} // namespace lucid::cpu
