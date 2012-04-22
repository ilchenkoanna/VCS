using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Rep
{
    [global::System.AttributeUsage(AttributeTargets.All, Inherited = false, AllowMultiple = false)]
    sealed class CompleteAttribute : Attribute
    {
        public CompleteAttribute()
        {
        }

    }
}
