// SPDX-License-Identifier: MIT
// Compatibility surface for the .NET 4.5 profile, not emulator logic.
global using System;
global using System.IO;
global using System.Collections.Generic;
global using System.Linq;
global using System.Threading;
namespace System.Runtime.CompilerServices {
    internal static class IsExternalInit { }
    [AttributeUsage(AttributeTargets.All)]
    internal sealed class RequiredMemberAttribute : Attribute { }
    [AttributeUsage(AttributeTargets.All)]
    internal sealed class CompilerFeatureRequiredAttribute : Attribute {
        public CompilerFeatureRequiredAttribute(string featureName) { FeatureName = featureName; }
        public string FeatureName { get; }
        public bool IsOptional { get; set; }
    }
}
namespace System.Diagnostics.CodeAnalysis {
    [AttributeUsage(AttributeTargets.Constructor)]
    internal sealed class SetsRequiredMembersAttribute : Attribute { }
}
namespace Serilog {
    // Debug logging is intentionally excluded from this deterministic probe.
    internal static class Log {
        [System.Diagnostics.Conditional("GB_VERBOSE_LOGGING")]
        public static void Debug(string message, params object[] args) { }
        [System.Diagnostics.Conditional("GB_VERBOSE_LOGGING")]
        public static void Information(string message, params object[] args) { }
        [System.Diagnostics.Conditional("GB_VERBOSE_LOGGING")]
        public static void Warning(string message, params object[] args) { }
    }
}
