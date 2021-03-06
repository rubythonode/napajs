// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "javascript-module-loader.h"
#include "module-cache.h"
#include "module-loader-helpers.h"

#include <napa/v8-helpers.h>

using namespace napa;
using namespace napa::module;

JavascriptModuleLoader::JavascriptModuleLoader(BuiltInModulesSetter builtInModulesSetter, ModuleCache& moduleCache)
    : _builtInModulesSetter(std::move(builtInModulesSetter)) , _moduleCache(moduleCache) {}

bool JavascriptModuleLoader::TryGet(const std::string& path, v8::Local<v8::Object>& module) {
    auto isolate = v8::Isolate::GetCurrent();
    v8::EscapableHandleScope scope(isolate);

    auto source = module_loader_helpers::ReadModuleFile(path);
    JS_ENSURE_WITH_RETURN(isolate, !source.IsEmpty(), false, "Can't read Javascript module: \"%s\"", path.c_str());

    auto context = isolate->GetCurrentContext();

    auto moduleContext = v8::Context::New(isolate);
    JS_ENSURE_WITH_RETURN(isolate, !moduleContext.IsEmpty(), false, "Can't create module context for: \"%s\"", path.c_str());

    // We set an empty security token so callee can access caller's context.
    moduleContext->SetSecurityToken(v8::Undefined(isolate));
    v8::Context::Scope contextScope(moduleContext);

    module_loader_helpers::SetupModuleContext(context, moduleContext, path);

    _builtInModulesSetter(moduleContext);

    // To prevent cycle, cache unloaded module first.
    _moduleCache.Upsert(path, module_loader_helpers::ExportModule(moduleContext->Global(), nullptr));

    v8::TryCatch tryCatch;
    {
        auto origin = v8::ScriptOrigin(v8_helpers::MakeV8String(isolate, path));
        auto script = v8::Script::Compile(source, &origin);
        if (script.IsEmpty() || tryCatch.HasCaught()) {
            tryCatch.ReThrow();
            return false;
        }

        auto run = script->Run();
        if (run.IsEmpty() || tryCatch.HasCaught()) {
            tryCatch.ReThrow();
            return false;
        }
    }

    // Export a loaded module.
    module = scope.Escape(module_loader_helpers::ExportModule(moduleContext->Global(), nullptr));
    return true;
}