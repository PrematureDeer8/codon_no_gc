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
    ret_sfs[instr] = findLast<ir::SeriesFlow>();
}

void AliasGenerator::nested_instr_handler(ir::CallInstr* instr){
    for(auto it = instr->begin(); it != instr->end(); it++){
        ir::Value* arg = *it;
        if(auto* nested_call = ir::cast<ir::CallInstr>(arg)){ 
            if(auto *vv = ir::cast<ir::VarValue>(nested_call->getCallee())){
                if(auto *func = ir::cast<ir::Func>(vv->getVar())){
                    auto ret = allocates_memory.find(func);
                    if(ret != allocates_memory.end()){
                        if(allocates_memory[func] == Allocates::TRUE){

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
                        }else if(allocates_memory[func] == Allocates::UNKNOWN){
                            if(auto* bodied_func = ir::cast<BodiedFunc>(func)){
                                for(auto* var : global_vars[bodied_func]){
                                   generated_aliases[var] = findLast<ir::SeriesFlow>();
                                }
                            }
                        }
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
     // if parent function is unknown then
    // create global variable flag
    if(is_unknown && heap_sf.find(findLast<ir::SeriesFlow>()) != heap_sf.end()){
        auto *global_v = M->Nr<ir::Var>(M->getBoolType(), true, false, true);
        auto* assign_global = M->Nr<ir::AssignInstr>(global_v, M->Nr<ir::BoolConst>(true, M->getBoolType()));
        if(current_func){
            global_vars[current_func].push_back(global_v);
        }
        ir::util::Operator::insertBefore(assign_global);
    }
    ir::util::Operator::handle(instr);

}

void AliasGenerator::handle(ir::CallInstr *instr){
    if(AliasGenerator::depth() > 1){
        nested_instr_handler(instr);
    }
    ir::util::Operator::handle(instr);
}

void AliasGenerator::handle(ir::SeriesFlow *flow){

    for (auto it = flow->begin(); it != flow->end(); ++it) {
        if(auto *instr = ir::cast<ir::CallInstr>(*it)){
            if(auto *vv = ir::cast<ir::VarValue>(instr->getCallee())){
                if(auto *func = ir::cast<ir::Func>(vv->getVar())){
                    auto ret = allocates_memory.find(func);
                    if(ret != allocates_memory.end()){
                        if(allocates_memory[func] == Allocates::TRUE){
                            bool global = current_func == nullptr;
                            auto *v = M->Nr<ir::Var>(instr->getType(), global);

                            //this variable is on the heap!
                            auto *assign_instr = M->Nr<ir::AssignInstr>(v, instr);

                            *it = assign_instr;

                            if(!global){
                                current_func->push_back(v);
                            }
                            generated_aliases[v] = flow;
                        }else if(allocates_memory[func] == Allocates::UNKNOWN){
                            if(auto* bodied_func = ir::cast<BodiedFunc>(func)){
                                for(auto* var : global_vars[bodied_func]){
                                   generated_aliases[var] = flow;
                                }
                            }
                        }
                    }
                }
            }    
        }
    }
    ir::util::Operator::handle(flow);
}

// instr->getValue() --> gets the arguments
bool GCFree::tracesToSeqAlloc(ir::Value* val){
    return false;
}

Allocates GCFree::checkFunctionAllocates(ir::Func* func, std::unordered_set<ir::Func*>& visited) {
    // 1. Check if we already computed this
    if (allocates_memory.find(func) != allocates_memory.end()) {
        return allocates_memory[func];
    }

    // 2. Break nar(), bar() calls foo())
    if (visited.count(func)) {
        return Allocates::FALSE; 
    }
    visited.insert(func);

    // base case
    // check if function is malloc function
    std::string name = func->getUnmangledName();
    bool is_allocator = (name == "seq_alloc" || name == "seq_alloc_atomic" || 
                                name == "seq_alloc_uncollectable" || name == "seq_alloc_atomic_uncollectable");
    if(is_allocator){
        return Allocates::TRUE;
    }

    Allocates ret;
    // 3. It's a standard function. Find its returns!
    // find the return statements in the bodied function
    if(auto* bodied_func = ir::cast<ir::BodiedFunc>(func)){
        ReturnFinder finder;
        bodied_func->getBody()->accept(finder);
        return_statements[bodied_func] = finder.returns;

        // counter keeps track of current index 
        int counter = 0;
        int ret_counter = 0;
        bool unknown_ret = false;
        for(auto* r : finder.returns){
            auto* val = r->getValue();
            if(auto* flow_instr = ir::cast<ir::FlowInstr>(val)){
                // get the first instruction cause there should only be one
                if(auto* ret_val = flow_instr->getValue()){
                    val = ret_val;
                }else if(auto *series_flow = ir::cast<ir::SeriesFlow>(flow_instr->getFlow())){
                    val = series_flow->front();
                }
            }
            // check for heap allocating call instructions
            if(auto* call_instr = ir::cast<ir::CallInstr>(val)){
                if(auto* var_val = ir::cast<ir::VarValue>(call_instr->getCallee())){
                    // check if func allocates
                    if(auto* callee_func = ir::cast<ir::Func>(var_val->getVar())){
                        counter++;
                        Allocates ret = checkFunctionAllocates(callee_func, visited);
                        if(ret == Allocates::UNKNOWN){
                            unknown_ret = true;
                        // identify allocation of parameter variables for callee_func
                        }else if(ret == Allocates::FALSE){
                            // check if function parameters escape
                            if(checkParametersEscape(call_instr)){
                                // check if any parameter are on heap
                                for(auto it = call_instr->begin(); it != call_instr->end(); ++it){
                                    if(auto* vv = ir::cast<ir::VarValue>(*it)){
                                        if(auto* var = ir::cast<ir::Var>(vv->getVar())){
                                            if(checkVarIsOnHeap(bodied_func, var, visited) == Allocates::TRUE){
                                                ret = Allocates::TRUE;
                                                break;
                                            }
                                        }
                                    }else if(auto* nested_call_instr = ir::cast<ir::CallInstr>(*it)){
                                            if(auto* var_v = ir::cast<ir::VarValue>(nested_call_instr->getCallee())){
                                                if(auto* callee_func = ir::cast<ir::Func>(var_v->getVar())){
                                                    if(checkFunctionAllocates(callee_func, visited) == Allocates::TRUE){
                                                        ret = Allocates::TRUE;
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                }
                            }
                        }
                        if(ret == Allocates::TRUE){
                            heap_sf.insert(finder.ret_sfs[r]);
                        }
                        ret_counter += static_cast<int>(ret);
                        
                    }
                }
            //check for heap allocated variables as well
            }else if(auto* vv = ir::cast<ir::VarValue>(val)){
                counter++;
                // TODO: handle global variable case
                // TODO: handle external variable case
                Allocates ret = checkVarIsOnHeap(bodied_func, vv->getVar(), visited);

                if(ret == Allocates::TRUE){
                    heap_sf.insert(finder.ret_sfs[r]);
                }

                if(ret == Allocates::UNKNOWN){
                    unknown_ret = true;
                }
                ret_counter += static_cast<int>(ret);
            }else if(auto* constant = ir::cast<ir::Const>(val)){
                counter++;

            }
            
            // this means that the function can return
            // both a stack allocation or heap allocation
            // depending on the series flow (if statement)
            if(ret_counter != 0 && ret_counter != counter){
                unknown_ret = true;
            }

        }
        if(unknown_ret){
            return Allocates::UNKNOWN;
        }
        if(ret_counter && counter){
            if(ret_counter == counter){
                return Allocates::TRUE;
            }
        }
    }
    return Allocates::FALSE;
}

void VarFinder::handle(ir::AssignInstr* instr){
    if(util::match(target_var, instr->getLhs())){
        if(auto *last_series_flow = findLast<ir::SeriesFlow>()){
            sf_last_assign[last_series_flow] = instr;
        }
    }
}

Allocates GCFree::checkVarIsOnHeap(ir::BodiedFunc* func, ir::Var* var, std::unordered_set<ir::Func*>& visited){
    // TODO: Build a cache of variables for faster access
    // loop through function for variable declaration
    VarFinder finder;
    finder.target_var = var;
    func->getBody()->accept(finder);
    int counter = 0;
    int ret_counter = 0;
    for(auto& [flow, assign_instr] : finder.sf_last_assign){
        if(auto* call_instr = ir::cast<ir::CallInstr>(assign_instr->getRhs())){
            if(auto *vv = ir::cast<ir::VarValue>(call_instr->getCallee())){
                if(auto* init_func = ir::cast<ir::Func>(vv->getVar())){
                    counter++;
                    Allocates ret = checkFunctionAllocates(init_func, visited);
                    if(ret == Allocates::UNKNOWN){
                        return ret;
                    }
                    ret_counter += static_cast<int>(ret);
                }
            }
        //handle the case when return variable is assigned to another variable
        }else if(auto* vv = ir::cast<ir::VarValue>(assign_instr->getRhs())){
            if(auto* rhs_var = ir::cast<ir::Var>(vv->getVar())){
                counter++;
                Allocates ret = checkVarIsOnHeap(func, rhs_var, visited);
                if(ret == Allocates::UNKNOWN){
                    return ret;
                }
                ret_counter += static_cast<int>(ret);
            }
        }

        // this means that the function can return
        // both a stack allocation or heap allocation
        // depending on the series flow (if statement)
        if(ret_counter && ret_counter != counter){
            return Allocates::UNKNOWN;
        }

    }
    if(ret_counter && counter){
        if(ret_counter == counter){
            return Allocates::TRUE;
        }
    }
    return Allocates::FALSE;
}

bool GCFree::checkParametersEscape(ir::CallInstr* call_instr){

    // heuristic:
    // if function returns an obj or pointer
    // then we assume that the parameters escaped

    if(auto* vv = ir::cast<ir::VarValue>(call_instr->getCallee())){
        if(auto* func = ir::cast<ir::Func>(vv->getVar())){
            ir::Type* generic_type = func->getType();
            if(auto* func_type = ir::cast<ir::FuncType>(generic_type)){
                ir::Type* ret_type = func_type->getReturnType();
                // TODO: create recursive call to analyze record type
                if(
                    ir::cast<ir::PointerType>(ret_type)
                ||  ir::cast<ir::RefType>(ret_type)
                ||  ir::cast<ir::RecordType>(ret_type)
                ){
                    return true;
                }
            }
        }
    }
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
        generator.heap_sf = heap_sf; // TODO: Figure out how to pass by reference instead
        auto* func = ir::cast<ir::Func>(bodied_func);
        if(func && generator.allocates_memory[func] == Allocates::UNKNOWN){
            generator.is_unknown = true;
        }else{
            generator.is_unknown = false;
        }
        bodied_func->getBody()->accept(generator);


        //assess if function is returning heap allocated structures
        for(auto* ret_instr : return_statements[bodied_func]){
            auto* val = ret_instr->getValue();
            // nested_call function in the ret_instr
            if(auto* call_instr = ir::cast<ir::CallInstr>(val)){
                // check if return type indicates parameters escape call
                if(checkParametersEscape(call_instr)){
                    // erase any parameters in generated aliases 
                    // so that they don't end up being freed
                    // TODO: make this faster 
                    for(auto it = call_instr->begin(); it != call_instr->end(); ++it){
                        if(auto* vv = ir::cast<ir::VarValue>(*it)){
                            if(auto* var = ir::cast<ir::Var>(vv->getVar())){
                                generator.generated_aliases.erase(var);
                            }
                        }
                    }
                }
                
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