 

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Rep.Lib
{
    [Complete]
    public interface Treeish
    {
        ObjectId GetTreeId();
        Tree GetTree();
    }
}
