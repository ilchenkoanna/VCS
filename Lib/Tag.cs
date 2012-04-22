 

using System;
using System.IO;
using System.Text;
using Rep.Exceptions;
using Rep.Util;

namespace Rep.Lib
{
  public class Tag {
      public class Constants
      {
          public static readonly string TagsPrefix = "refs/tags";
      }

      public Repository Repository { get; protected set; }

	

	

	private byte[] raw;

	


	/**
	 * Construct a new, yet unnamed Tag.
	 *
	 * @param db
	 */
	public Tag( Repository db) {
		Repository = db;
	}

	/**
	 * Construct a Tag representing an existing with a known name referencing an known object.
	 * This could be either a simple or annotated tag.
	 *
	 * @param db {@link Repository}
	 * @param id target id.
	 * @param refName tag name or null
	 * @param raw data of an annotated tag.
	 */
	public Tag(Repository db, ObjectId id, String refName, byte[] raw) {
		Repository = db;
		if (raw != null) {
			TagId = id;
			Id = ObjectId.FromString(raw, 7);
		} else
			Id = id;
		if (refName != null && refName.StartsWith("refs/tags/"))
			refName = refName.Substring(10);
		TagName = refName;
		this.raw = raw;
	}

	/**
	 * @return tagger of a annotated tag or null
	 */
    private PersonIdent author;
	public PersonIdent Author {
        get
        {
            decode();
            return author;
        }
        set
        {
            author = value;
        }
	}

	/**
	 * @return comment of an annotated tag, or null
	 */
    private String message;
	public String Message {
        get
        {
            decode();
            return message;
        }
        set
        {
            message = value;
        }
	}

    private void decode()
    {
        // FIXME: handle I/O errors
        if (raw == null) return;

        using (var br = new StreamReader(new MemoryStream(raw)))
        {
            String n = br.ReadLine();
            if (n == null || !n.StartsWith("object "))
            {
                throw new CorruptObjectException(TagId, "no object");
            }
            Id = ObjectId.FromString(n.Substring(7));
            n = br.ReadLine();
            if (n == null || !n.StartsWith("type "))
            {
                throw new CorruptObjectException(TagId, "no type");
            }
            TagType = n.Substring("type ".Length);
            n = br.ReadLine();

            if (n == null || !n.StartsWith("tag "))
            {
                throw new CorruptObjectException(TagId, "no tag name");
            }
            TagName = n.Substring("tag ".Length);
            n = br.ReadLine();

            // We should see a "tagger" header here, but some repos have tags
            // without it.
            if (n == null)
                throw new CorruptObjectException(TagId, "no tagger header");

            if (n.Length > 0)
                if (n.StartsWith("tagger "))
                    Tagger = new PersonIdent(n.Substring("tagger ".Length));
                else
                    throw new CorruptObjectException(TagId, "no tagger/bad header");

            // Message should start with an empty line, but
            StringBuilder tempMessage = new StringBuilder();
            char[] readBuf = new char[2048];
            int readLen;
            int readIndex = 0;
            while ((readLen = br.Read(readBuf, readIndex, readBuf.Length)) > 0)
            {
                //readIndex += readLen;
                tempMessage.Append(readBuf, 0, readLen);
            }
            message = tempMessage.ToString();
            if (message.StartsWith("\n"))
                message = message.Substring(1);
        }

        raw = null;
    }


      /**
	 * Store a tag.
	 * If author, message or type is set make the tag an annotated tag.
	 *
	 * @throws IOException
	 */
	public void Save(){ //renamed from Tag
		if (TagId != null)
			throw new InvalidOperationException("exists " + TagId);
		ObjectId id;

	    if (author!=null || message!=null || tagType!=null) {
			ObjectId tagid = new ObjectWriter(Repository).WriteTag(this);
			TagId = tagid;
			id = tagid;
		} else {
			id = Id;
		}

		RefUpdate ru = Repository.UpdateRef(Constants.TagsPrefix  + "/" + TagName);
		ru.SetNewObjectId(id);
		ru.SetRefLogMessage("tagged " + TagName, false);
		if (ru.ForceUpdate() == RefUpdate.Result.LockFailure)
			throw new ObjectWritingException("Unable to lock tag " + TagName);
	}

	public String toString() {
		return "tag[" + TagName + TagType + Id + " " + Author + "]";
	}

    public ObjectId TagId { get; set; }


	/**
	 * @return creator of this tag.
	 */
	public PersonIdent Tagger{
        get
        {
            return Author;
        }
        set
        {
            Author = value;
        }
	}


	/**
	 * @return tag target type
	 */
    private String tagType;
	public String TagType {
        get
        {
            decode();
            return tagType;
        }
        set
        {
            tagType = value;
        }
	}

	
    public string TagName { get; set; }

	/**
	 * @return the SHA'1 of the object this tag refers to.
	 */

    public ObjectId Id { get; set; }

	/**
	 * Set the id of the object this tag refers to.
	 *
	 * @param objId
	 */
}
}
