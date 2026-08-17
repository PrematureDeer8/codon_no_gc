#include "codon/cir/transform/pass.h"
#include "codon/cir/cir.h"
#include "codon/cir/util/visitor.h"
#include "codon/cir/util/irtools.h"
#include "codon/cir/util/matching.h"
#include "codon/cir/type.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace codon{
namespace ir {
namespace transform {
namespace insertion {

enum class Allocates {
    FALSE,
    TRUE,
    UNKNOWN
};


// -----------------------------------------------------------------------------
// 1. The Helper Visitor: Hunts down every return statement in a function
// -----------------------------------------------------------------------------
struct ReturnFinder : public ir::util::Operator {
    std::vector<ir::ReturnInstr*> returns;
    // for a given return instruction, holds the corresponding seriesflow
    std::unordered_map<ir::ReturnInstr*, ir::SeriesFlow*> ret_sfs;

    void handle(ir::ReturnInstr *instr) override;
};

struct VarFinder : public ir::util::Operator {
    ir::Var* target_var = nullptr;
    std::unordered_map<ir::SeriesFlow*, ir::AssignInstr*> sf_last_assign;

    void handle(ir::AssignInstr *instr) override;
};

struct AliasGenerator : public ir::util::Operator {
    ir::Module* M;

    ir::BodiedFunc* current_func = nullptr;
    ir::SeriesFlow* current_block = nullptr;
    bool is_unknown;
    

    std::unordered_set<ir::SeriesFlow*> heap_sf;
    std::unordered_map<ir::Func*, Allocates> allocates_memory;
    // keys of this dict hold variables that belong to functions
    // that create variables either on the heap or stack, hence unknown
    // values hold global variable flags to indicate if heap allocation is made
    std::unordered_map<ir::BodiedFunc*, std::vector<ir::Var*>> global_vars;
    // generated_aliases are candidates for inserting GC frees
    // these variables are deemed to be in a function's local scope (escape analysis)
    std::unordered_map<ir::Var*, ir::SeriesFlow*> generated_aliases;

    // void handle(ir::ReturnInstr *instr) override;
    void handle(ir::CallInstr *instr) override;
    void nested_instr_handler(ir::CallInstr *instr);
    void handle(ir::ReturnInstr *instr) override;
    void handle(ir::SeriesFlow *flow) override;
    void global_var_gen(ir::BodiedFunc* bodied_func, std::vector<ir::ReturnInstr*>& heap_returns);

};

// struct

// -----------------------------------------------------------------------------
// 2. The Core GC Pass
// -----------------------------------------------------------------------------
class GCFree : public Pass {
private:
    // The memoization cache: remembers if a function returns heap memory
    std::unordered_map<ir::Func*, Allocates> allocates_memory;
    std::unordered_map<ir::BodiedFunc*, std::vector<ir::ReturnInstr*>> return_statements;
    // set describes the returns that are associated with heap allocations returns
    std::unordered_set<ir::SeriesFlow*> heap_sf;

    // TODO: Implement your traceback logic here
    bool tracesToSeqAlloc(ir::Value* val);

    // Recursive analyzer with cycle prevention
    Allocates checkFunctionAllocates(ir::Func* func, std::unordered_set<ir::Func*>& visited);
    Allocates checkVarIsOnHeap(ir::BodiedFunc* func, ir::Var* var, std::unordered_set<ir::Func*>& visited);
    bool checkParametersEscape(ir::CallInstr* call_instr);

    // insert GC free calls
    // void insertGCFree(ir::Value* val);

    void is_rvalue(ir::Value* val);

public:
    static const std::string KEY;
    std::string getKey() const override { return KEY; }

    // -------------------------------------------------------------------------
    // The main entry point called by the PassManager
    // -------------------------------------------------------------------------
    void run(ir::Module *module) override;

};

// Required static definition for the pass key
inline const std::string GCFree::KEY = "gc-free";

} // namespace insertion
}
}
}