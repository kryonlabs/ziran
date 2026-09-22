/* Named post-check invariants shared by every Ziran backend. */
#include "kir_laws.h"
#include "kir_diagnostic.h"

int
KirCheckLaws(KirProgram **programs, int count)
{
    int violations = 0;
    for(int p = 0; p < count; p++) {
        if(programs[p] == NULL)
            continue;
        for(int m = 0; m < programs[p]->module_count; m++) {
            KirModule *module = &programs[p]->modules[m];
            for(int f = 0; f < module->function_count; f++) {
                KirFunction *fn = &module->functions[f];
                for(int s = 0; s < fn->stmt_count; s++) {
                    KirStmt *statement = &fn->stmts[s];
                    if(statement->kind != KIR_STMT_BLOCK_CALL)
                        continue;
                    KirDiagnostic(statement->span, "ir.block_call.lowered",
                                  "law ir.block_call.lowered: unresolved block call %s",
                                  statement->callee);
                    violations++;
                }
            }
        }
    }
    return violations == 0;
}
