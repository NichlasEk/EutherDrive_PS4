// SPDX-License-Identifier: MIT
// PS4's managed BCL disables AppDomain.CreateDomain; use Mono's embedding API.
static void *(*case_domain_get)(void);
static void *(*case_domain_create)(char *, char *);
static int (*case_domain_set)(void *, int);
static void *(*case_assembly_load)(void *, const char *, int *, int);
static void *(*case_class)(void *, const char *, const char *);
static void *(*case_method)(void *, const char *, int);
static void *(*case_invoke)(void *, void *, void **, void **);
static const char *case_config;
static const char *benchmark_case_config(void) { return case_config; }
static int benchmark_case_init(int mono) {
#define CASE_RESOLVE(name, var) if (!resolve(mono, name, (void **)&var)) return 0
    CASE_RESOLVE("mono_domain_get",case_domain_get);
    CASE_RESOLVE("mono_domain_create_appdomain",case_domain_create);
    CASE_RESOLVE("mono_domain_set",case_domain_set);
    CASE_RESOLVE("mono_assembly_load_from_full",case_assembly_load);
    CASE_RESOLVE("mono_class_from_name",case_class);
    CASE_RESOLVE("mono_class_get_method_from_name",case_method);
    CASE_RESOLVE("mono_runtime_invoke",case_invoke);
#undef CASE_RESOLVE
    return 1;
}
static int benchmark_run_case(const char *config) {
    void *parent=case_domain_get();
    char name[]="benchmark-case";
    void *child=case_domain_create(name,NULL);
    if (!child || child==parent) { report("CASE ERROR cannot create isolated Mono domain"); return 0; }
    int okay=0;
    if (case_domain_set(child,1)) {
        case_config=config;
        int status=0;
        void *image=load_image("/app0/benchmark.exe",&status,0,0);
        if (image && case_assembly_load(image,"/app0/benchmark.exe",&status,0)) {
            void *klass=case_class(image,"MonoBenchmark","IsolatedCase");
            void *method=klass?case_method(klass,"Main",0):NULL;
            if (method) {
                void *exception=NULL;
                case_invoke(method,NULL,NULL,&exception);
                okay=exception==NULL;
            }
        }
    }
    if (!case_domain_set(parent,1)) { report("FAIL cannot restore parent Mono domain"); return 0; }
    case_config=NULL;
    // This BCL lacks the managed unload callback. Retain the ten bounded case
    // domains until final runtime shutdown; never unload them individually.
    return okay;
}
