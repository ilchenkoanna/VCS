 


using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace Rep.Lib.CSharp.Exceptions
{
    /**
  * An expected object is missing.
  */
    public class MissingObjectException : IOException
    {

        /**
         * Construct a MissingObjectException for the specified object id.
         * Expected type is reported to simplify tracking down the problem.
         *
         * @param id SHA-1
         * @param type object type
         */
        public MissingObjectException(ObjectId id, ObjectType type)
            : base("Missing " + type + " " + id)
        {
        }
    }
}
