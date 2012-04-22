using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace Rep.Lib.CSharp.Transport
{
    /**
     * Description of an object stored in a pack file, including offset.
     * <p>
     * When objects are stored in packs Git needs the ObjectId and the offset
     * (starting position of the object data) to perform random-access reads of
     * objects from the pack. This extension of ObjectId includes the offset.
     */
    public class PackedObjectInfo : ObjectId
    {

        public PackedObjectInfo(long headerOffset, int packedCRC, AnyObjectId id)
            : base(id)
        {
            Offset = headerOffset;
            CRC = packedCRC;
        }

        /**
         * Create a new structure to remember information about an object.
         *
         * @param id
         *            the identity of the object the new instance tracks.
         */
        public PackedObjectInfo(AnyObjectId id)
            : base(id)
        {
        }

        public long Offset { get; set; }
        public int CRC { get; set; }
    }
}
