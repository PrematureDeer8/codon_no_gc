#include "gc_free.h"
#include <iostream>

extern "C" void dump_cir(codon::ir::Node* node){
    if(node){
        std::cout << *node << std::endl;
    }else{
        std::cout << "(null node)" << std::endl;
    }
}

namespace codon{
namespace ir {
namespace transform {
namespace insertion {

void ReturnFinder::handle(ir::ReturnInstr *instr) {
    returns.push_back(instr);
}
// instr->getValue() --> gets the arguments
bool GCFree::tracesToSeqAlloc(ir::Value* val){
    return false;
}

bool GCFree::checkFunctionAllocates(ir::Func* func, std::unordered_set<ir::Func*>& visited) {
    // 1. Check if we already computed this
    if (allocates_memory.find(func) != allocates_memory.end()) {
        return allocates_memory[func];
    }

    // 2. Break infinite loops (e.g., foo() calls bar(), bar() calls foo())
    if (visited.count(func)) {
        return false; 
    }
    visited.insert(func);

    // base case
    // check if function is malloc function
    std::string name = func->getUnmangledName();
    bool is_allocator = (name == "seq_alloc" || name == "seq_alloc_atomic" || 
                                name == "seq_alloc_uncollectable" || name == "seq_alloc_atomic_uncollectable");
    if(is_allocator){
        return true;
    }

    // 3. It's a standard function. Find its returns!
    // find the return statements in the bodied function
    if(auto* bodied_func = ir::cast<ir::BodiedFunc>(func)){
        ReturnFinder finder;
        bodied_func->getBody()->accept(finder);

        
        for(auto* ret : finder.returns){
            auto* val = ret->getValue();
            if(auto* call_instr = ir::cast<ir::CallInstr>(val)){
                if(auto* var_val = ir::cast<ir::VarValue>(call_instr->getCallee())){
                    if(auto* callee_func = ir::cast<ir::Func>(var_val->getVar())){
                        return checkFunctionAllocates(callee_func, visited);
                    }
                }
            }
            // TODO: check for heap allocated variables as well

        }
    }
    // 4. Cache the result for O(1) lookups later
    return false;
}
void GCFree::run(ir::Module *module){
    
    //determine which functions allocate memory
    for (auto* var : *module) {
        std::unordered_set<ir::Func*> visited;
        if(auto *func = ir::cast<ir::Func>(var)){
            allocates_memory[func] = checkFunctionAllocates(func, visited);
        }
    }

    // --- PHASE 2: Insert the Free Calls ---
    // for (auto* func : *module) {
    //     auto* bodied_func = ir::cast<ir::BodiedFunc>(func);
    //     if (!bodied_func) continue;

    //     // TODO: Walk the 'bodied_func' instructions here.
    //     // When you see a CallInstr, do a quick lookup: 
    //     // if (allocates_memory[call_instr->getCallee()]) { ... }
    //     // If true, track the variable and insert a free() at the end of its scope!
    // }
}


}

}
}
}