#include "gc_free.h"
#include <iostream>
#include <unordered_set>

// for debug purposes 
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

void AliasGenerator::nested_instr_handler(ir::CallInstr* instr){
    for(auto it = instr->begin(); it != instr->end(); it++){
        ir::Value* arg = *it;
        if(auto* nested_call = ir::cast<ir::CallInstr>(arg)){ 
            if(auto *vv = ir::cast<ir::VarValue>(nested_call->getCallee())){
                if(auto *func = ir::cast<ir::Func>(vv->getVar())){
                    auto ret = allocates_memory.find(func);
                    if(ret != allocates_memory.end() && allocates_memory[func]){
                        bool global = current_func == nullptr;
                        auto *v = M->Nr<ir::Var>(nested_call->getType(), global);
                        // generated_aliases.insert(v);
                        auto* lastSeriesFlow = findLast<ir::SeriesFlow>();
                        generated_aliases[v] = lastSeriesFlow;
                        //handle global
                        // if(global){
                        //     static int counter = 1;
                        //     v->setName(".anon_global" + std::string(counter++));
                        // }
                        
                        //this variable is on the heap!
                        auto *assign_instr = M->Nr<ir::AssignInstr>(v, nested_call);
                        ir::util::Operator::insertBefore(assign_instr);

                        

                        if(!global){
                            current_func->push_back(v);
                        }

                        ir::VarValue *var_val = M->Nr<ir::VarValue>(v);
                        // replace nested_call instr with variable value
                        *it = var_val;
                    }
                }
            }
        }
    }
}


void AliasGenerator::handle(ir::ReturnInstr *instr){
    auto* val = instr->getValue();
    if(auto *nested_call = ir::cast<ir::CallInstr>(val)){
        nested_instr_handler(nested_call);
    }
    ir::util::Operator::handle(instr);

}

void AliasGenerator::handle(ir::CallInstr *instr){
    if(AliasGenerator::depth() == 1){
        if(auto *vv = ir::cast<ir::VarValue>(instr->getCallee())){
            if(auto *func = ir::cast<ir::Func>(vv->getVar())){
                auto ret = allocates_memory.find(func);
                if(ret != allocates_memory.end() && allocates_memory[func]){
                    bool global = current_func == nullptr;
                    auto *v = M->Nr<ir::Var>(instr->getType(), global);
                    auto* lastSeriesFlow = findLast<ir::SeriesFlow>();
                    generated_aliases[v] = lastSeriesFlow;

                    //this variable is on the heap!
                    auto *assign_instr = M->Nr<ir::AssignInstr>(v, instr);
                    // instr->replaceAll(var_val);
                    ir::util::Operator::insertBefore(assign_instr);


                    if(!global){
                        current_func->push_back(v);
                    }
                }
            }
        }    
    }else{
        nested_instr_handler(instr);
    }
    ir::util::Operator::handle(instr);
    /*
    for(auto it = instr->begin(); it != instr->end(); it++){
        ir::Value* arg = *it;
        if(auto* nested_call = ir::cast<ir::CallInstr>(arg)){ 
            if(auto *vv = ir::cast<ir::VarValue>(nested_call->getCallee())){
                if(auto *func = ir::cast<ir::Func>(vv->getVar())){
                    auto ret = allocates_memory.find(func);
                    if(ret != allocates_memory.end() && allocates_memory[func]){
                        bool global = current_func == nullptr;
                        auto *v = M->Nr<ir::Var>(nested_call->getType(), global);
                        //handle global
                        // if(global){
                        //     static int counter = 1;
                        //     v->setName(".anon_global" + std::string(counter++));
                        // }
                        
                        //this variable is on the heap!
                        auto *assign_instr = M->Nr<ir::AssignInstr>(v, nested_call);
                        ir::util::Operator::insertBefore(assign_instr);

                        

                        if(!global){
                            current_func->push_back(v);
                        }

                        ir::VarValue *var_val = M->Nr<ir::VarValue>(v);
                        // replace nested_call instr with variable value
                        *it = var_val;
                    }
                }
            }
        }
    }
    */
    // create temp for current call instruction
    

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
        return_statements[bodied_func] = finder.returns;

        
        for(auto* ret : finder.returns){
            auto* val = ret->getValue();
            if(auto* flow_instr = ir::cast<ir::FlowInstr>(val)){
                // get the first instruction cause there should only be one
                if(auto *series_flow = ir::cast<ir::SeriesFlow>(flow_instr->getFlow())){
                    val = series_flow->front();
                }
            }
            // check for heap allocating call instructions
            if(auto* call_instr = ir::cast<ir::CallInstr>(val)){
                if(auto* var_val = ir::cast<ir::VarValue>(call_instr->getCallee())){
                    // check if func allocates
                    if(auto* callee_func = ir::cast<ir::Func>(var_val->getVar())){
                        return checkFunctionAllocates(callee_func, visited);
                    }
                }
            //check for heap allocated variables as well
            }else if(auto* vv = ir::cast<ir::VarValue>(val)){
                // TODO: handle global variable case
                // TODO: handle external variable case
                return checkVarIsOnHeap(bodied_func, vv->getVar(), visited);
            }

        }
    }
    return false;
}

void VarFinder::handle(ir::AssignInstr* instr){
    if(util::match(target_var, instr->getLhs())){
        if(auto *last_series_flow = findLast<ir::SeriesFlow>()){
            sf_last_assign[last_series_flow] = instr;
        }
    }
}

bool GCFree::checkVarIsOnHeap(ir::BodiedFunc* func, ir::Var* var, std::unordered_set<ir::Func*>& visited){
    bool ret = true;
    // loop through function for variable declaration
    VarFinder finder;
    finder.target_var = var;
    func->getBody()->accept(finder);
    for(auto& [flow, assign_instr] : finder.sf_last_assign){
        if(auto* call_instr = ir::cast<ir::CallInstr>(assign_instr->getRhs())){
            if(auto *vv = ir::cast<ir::VarValue>(call_instr->getCallee())){
                if(auto* init_func = ir::cast<ir::Func>(vv->getVar())){
                    ret &= checkFunctionAllocates(init_func, visited);
                }
            }
        //handle the case when return variable is assigned to another variable
        }else if(auto* vv = ir::cast<ir::VarValue>(assign_instr->getRhs())){
            if(auto* rhs_var = ir::cast<ir::Var>(vv->getVar())){
                ret &= checkVarIsOnHeap(func, rhs_var, visited);
            }
        }else{
            return false;
        }
    }
    if(finder.sf_last_assign.empty()){
        return false;
    }
    return ret;
}

void GCFree::run(ir::Module *module){
    
    //determine which functions allocate memory
    for (auto* var : *module) {
        std::unordered_set<ir::Func*> visited;
        if(auto *func = ir::cast<ir::Func>(var)){
            allocates_memory[func] = checkFunctionAllocates(func, visited);
        }
    }

    std::vector<ir::BodiedFunc*> all_functions;

    // Get standard functions
    for (auto* func : *module) {
        if (auto* bodied = ir::cast<ir::BodiedFunc>(func)) {
            all_functions.push_back(bodied);
        }
    }

    // Get the main entry point (the top-level script)
    if (auto* main_func = ir::cast<ir::BodiedFunc>(module->getMainFunc())) {
        // Prevent duplicates if main happens to be in the iterator
        if (std::find(all_functions.begin(), all_functions.end(), main_func) == all_functions.end()) {
            all_functions.push_back(main_func);
        }
    }

    // --- PHASE 2: Insert the Free Calls ---
    for (auto* bodied_func : all_functions) {
        // TODO: HANDLE FUNCTIONS THAT are not BODIED FUNC

        // TODO: Walk the 'bodied_func' instructions here.
        // generate temporary variables for heap allocated structures
        AliasGenerator generator;
        generator.M = module;
        generator.current_func = bodied_func;
        generator.allocates_memory = allocates_memory; // TODO: Figure out how to pass by reference instead

        bodied_func->getBody()->accept(generator);


        //assess if function is returning heap allocated structures
        for(auto* ret_instr : return_statements[bodied_func]){
            auto* val = ret_instr->getValue();
            // nested_call function in the ret_instr
            if(auto* call_instr = ir::cast<ir::CallInstr>(val)){
                // recurse in the call_instr
            }else if(auto* var = ir::cast<ir::Var>(val)){
                // variables being returned, escaped the local scope
                generator.generated_aliases.erase(var);
            }
        }

        // Logic handling for insertion of GC Frees
        for(const auto& pair : generator.generated_aliases){
            ir::Var* var_to_free = pair.first;
            ir::SeriesFlow* seriesflow = pair.second;
            ir::Func* deconstructor = module->getOrRealizeMethod(var_to_free->getType(), "__del__", {var_to_free->getType()});
            ir::CallInstr* gc_free_call = nullptr;

            if(deconstructor){
                ir::VarValue* param = module->Nr<ir::VarValue>(var_to_free);                    
                gc_free_call = ir::util::call(deconstructor, {param});
            // free pointer
            }else if(
                util::match(var_to_free->getType(), module->unsafeGetPointerType(module->getStringType())) 
            ||  util::match(var_to_free->getType(), module->unsafeGetPointerType(0))
                
            ){
                deconstructor = module->getOrRealizeFunc(
                    "free", 
                    {module->getPointerType()},
                    {},
                    {"std.internal.gc"}
                );
                if(deconstructor){
                    ir::VarValue* param = module->Nr<ir::VarValue>(var_to_free);                    
                    gc_free_call = ir::util::call(deconstructor, {param});
                }
                
            }

            // TODO: handle case when return instruction is 
            // last instruction in the seriesflow
            // insert GC free
            if(gc_free_call){
                seriesflow->push_back(gc_free_call);
            }
        }
    }
}


}

}
}
}