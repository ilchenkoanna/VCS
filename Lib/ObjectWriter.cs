 

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Rep.Lib
{
    public class ObjectWriter
    {
        public ObjectWriter(Repository repo)
        {
            throw new NotImplementedException();
        }

        internal ObjectId WriteTag(Tag tag)
        {
            throw new NotImplementedException();
        }

        internal ObjectId WriteBlob(System.IO.FileInfo fileInfo)
        {
            throw new NotImplementedException();
        }

        internal ObjectId WriteTree(Tree t)
        {
            throw new NotImplementedException();
        }

        internal ObjectId WriteCommit(Commit t)
        {
            throw new NotImplementedException();
        }
    }
}
