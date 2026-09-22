/* Named post-check invariants shared by every Ziran backend. */
#include "zir_laws.h"
#include "zir_diagnostic.h"

int
ZirCheckLaws(ZirProgram **programs, int count)
{
    int violations = 0;
    for(int p = 0; p < count; p++) {
        if(programs[p] == NULL)
            continue;
        for(int m = 0; m < programs[p]->module_count; m++) {
            ZirModule *module = &programs[p]->modules[m];
            for(int f = 0; f < module->function_count; f++) {
                ZirFunction *fn = &module->functions[f];
                for(int s = 0; s < fn->stmt_count; s++) {
                    ZirStmt *statement = &fn->stmts[s];
                    if(statement->kind != ZIR_STMT_BLOCK_CALL)
                        continue;
                    ZirDiagnostic(statement->span, "ir.block_call.lowered",
                                  "law ir.block_call.lowered: unresolved block call %s",
                                  statement->callee);
                    violations++;
                }
            }
        }
    }
    return violations == 0;
}
