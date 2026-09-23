#include "zir.h"
#include "zir_bundle.h"
#include "zir_check.h"
#include "zir_parse.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const ZirModule *
module_named(const ZirProgram *program, const char *name)
{
    for(int i = 0; i < program->module_count; i++) {
        if(strcmp(program->modules[i].name, name) == 0)
            return &program->modules[i];
    }
    return NULL;
}

static int
has_type(const ZirModule *module, const char *name)
{
    for(int i = 0; i < module->type_count; i++) {
        if(strcmp(module->types[i].name, name) == 0)
            return 1;
    }
    return 0;
}

int
main(void)
{
    static const char shapes[] =
        "#module \"shapes\"\n"
        "Point :: struct {\n"
        "    x: i32\n"
        "}\n"
        "Rectangle :: struct {\n"
        "    origin: Point\n"
        "    width: i32\n"
        "}\n"
        "Unused :: struct {\n"
        "    value: i32\n"
        "}\n";
    static const char operations[] =
        "#module \"operations\"\n"
        "#import \"shapes\"\n"
        "Measure :: (rect: Rectangle) -> i32 #export {\n"
        "    return rect.origin.x + rect.width\n"
        "}\n";
    static const char application[] =
        "#module \"application\"\n"
        "#import \"operations\"\n"
        "#import \"shapes\"\n"
        "Answer :: () -> i32 #export {\n"
        "    rect: Rectangle\n"
        "    rect.origin.x = 2\n"
        "    rect.width = 40\n"
        "    return Measure(rect)\n"
        "}\n";
    ZirProgram *programs[] = {
        parse_source_text("shapes.zi", shapes),
        parse_source_text("operations.zi", operations),
        parse_source_text("application.zi", application)
    };
    ZirProgram merged = {0};
    ZirProgram *linked;
    const ZirModule *shape_module;
    const ZirModule *operation_module;
    const ZirModule *application_module;

    for(int i = 0; i < 3; i++)
        assert(programs[i] != NULL);
    assert(CheckPrograms(programs, 3, 1));
    for(int i = 0; i < 3; i++)
        merged.module_count += programs[i]->module_count;
    merged.modules = calloc((size_t)merged.module_count, sizeof(*merged.modules));
    assert(merged.modules != NULL);
    int next = 0;
    for(int i = 0; i < 3; i++) {
        for(int m = 0; m < programs[i]->module_count; m++)
            merged.modules[next++] = programs[i]->modules[m];
    }
    ZirProgram *merged_ptr = &merged;
    assert(LinkImports(&merged_ptr, 1));
    linked = BundleLink(&merged, "application", "Answer");
    assert(linked != NULL);
    shape_module = module_named(linked, "shapes");
    operation_module = module_named(linked, "operations");
    application_module = module_named(linked, "application");
    assert(shape_module != NULL);
    assert(operation_module != NULL);
    assert(application_module != NULL);
    assert(has_type(shape_module, "Point"));
    assert(has_type(shape_module, "Rectangle"));
    assert(!has_type(shape_module, "Unused"));
    assert(shape_module->function_count == 0);
    assert(operation_module->function_count == 1);
    assert(application_module->function_count == 1);
    assert(FindType(application_module, "Rectangle", NULL) != NULL);
    assert(FindType(operation_module, "Point", NULL) != NULL);

    ProgramFree(linked);
    free(merged.modules);
    for(int i = 0; i < 3; i++)
        ProgramFree(programs[i]);
    return 0;
}
