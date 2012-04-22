﻿ 

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Rep.Exceptions;
using Rep.Lib.CSharp.Exceptions;
using System.IO;

namespace Rep.Lib
{
    public class Commit : Treeish
    {
        private byte[] raw;

        /**
         * Create an empty commit object. More information must be fed to this
         * object to make it useful.
         *
         * @param db
         *            The repository with which to associate it.
         */
        public Commit(Repository db)
            : this(db, new ObjectId[0])
        {
        }
        /**
         * Create a commit associated with these parents and associate it with a
         * repository.
         *
         * @param db
         *            The repository to which this commit object belongs
         * @param parentIds
         *            Id's of the parent(s)
         */
        public Commit(Repository db, ObjectId[] parentIds)
        {
            Repository = db;
            ParentIds = parentIds;
        }
        /**
         * Create a commit object with the specified id and data from and existing
         * commit object in a repository.
         *
         * @param db
         *            The repository to which this commit object belongs
         * @param id
         *            Commit id
         * @param raw
         *            Raw commit object data
         */
        public Commit(Repository db, ObjectId id, byte[] raw)
        {
            Repository = db;
            CommitId = id;
            treeId = ObjectId.FromString(raw, 5);
            ParentIds = new ObjectId[1];
            int np = 0;
            int rawPtr = 46;
            while (true)
            {
                if (raw[rawPtr] != 'p')
                    break;
                switch (np)
                {
                    case 0:
                        ParentIds[np++] = ObjectId.FromString(raw, rawPtr + 7);
                        break;
                    case 1:
                        ParentIds = new[] { ParentIds[0], ObjectId.FromString(raw, rawPtr + 7) };
                        np++;
                        break;
                    default:
                        if (ParentIds.Length <= np)
                        {
                            ObjectId[] old = ParentIds;
                            ParentIds = new ObjectId[ParentIds.Length + 32];
                            for (int i = 0; i < np; ++i)
                                ParentIds[i] = old[i];
                        }
                        ParentIds[np++] = ObjectId.FromString(raw, rawPtr + 7);
                        break;
                }
                rawPtr += 48;
            }
            if (np != ParentIds.Length)
            {
                ObjectId[] old = ParentIds;
                ParentIds = new ObjectId[np];
                for (int i = 0; i < np; ++i)
                    ParentIds[i] = old[i];
            }
            else
                if (np == 0)
                    ParentIds = new ObjectId[0];
            this.raw = raw;
        }

        #region Treeish Members

        private ObjectId treeId;
        private Tree tree;
        public ObjectId GetTreeId()
        {
            return treeId;
        }

        /**
         * Set the tree id for this commit object
         *
         * @param id
         */
        public void setTreeId(ObjectId id)
        {
            if (treeId == null || !treeId.Equals(id))
            {
                tree = null;
            }
            treeId = id;
        }

        public Tree GetTree()
        {
            if (tree == null)
            {
                tree = Repository.MapTree(GetTreeId());
                if (tree == null)
                    throw new MissingObjectException(GetTreeId(), ObjectType.Tree);
            }
            return tree;
        }


        /**
         * Set the tree object for this commit
         * @see #setTreeId
         * @param t the Tree object
         */
        public void SetTree(Tree t)
        {
            treeId = t.GetTreeId();
            tree = t;
        }

        #endregion

        public ObjectId CommitId { get; set; }
        public ObjectId[] ParentIds { get; set; }
        public Encoding Encoding { get; set; }
        public Repository Repository { get; protected set; }

        private string message;
        public string Message
        {
            get
            {
                Decode();
                return message;
            }
            set
            {
                message = value;
            }
        }

        private PersonIdent committer;
        public PersonIdent Committer
        {
            get
            {
                Decode();
                return committer;
            }
            set
            {
                committer = value;
            }
        }

        private PersonIdent author;
        public PersonIdent Author
        {
            get
            {
                Decode();
                return author;
            }
            set
            {
                author = value;
            }
        }

        private void Decode()
        {
            if (raw == null) return;

            using (var reader = new StreamReader(new MemoryStream(raw)))
            {
                String n = reader.ReadLine();
                if (n == null || !n.StartsWith("tree "))
                {
                    throw new CorruptObjectException(CommitId, "no tree");
                }
                while ((n = reader.ReadLine()) != null && n.StartsWith("parent "))
                {
                    // empty body
                }
                if (n == null || !n.StartsWith("author "))
                {
                    throw new CorruptObjectException(CommitId, "no author");
                }
                String rawAuthor = n.Substring("author ".Length);
                n = reader.ReadLine();
                if (n == null || !n.StartsWith("committer "))
                {
                    throw new CorruptObjectException(CommitId, "no committer");
                }
                String rawCommitter = n.Substring("committer ".Length);
                n = reader.ReadLine();

                if (n != null && n.StartsWith("encoding"))
                    Encoding = Encoding.GetEncoding(n.Substring("encoding ".Length));
                else if (n == null || !n.Equals(""))
                    throw new CorruptObjectException(CommitId, "malformed header:" + n);



#warning This does not currently support custom encodings
                //byte[] readBuf = new byte[br.available()]; // in-memory stream so this is all bytes left
                //br.Read(readBuf);
                //int msgstart = readBuf.Length != 0 ? (readBuf[0] == '\n' ? 1 : 0) : 0;

                if (Encoding != null)
                {
                    // TODO: this isn't reliable so we need to guess the encoding from the actual content
                    throw new NotSupportedException("Custom Encoding is not currently supported.");
                    //author = new PersonIdent(new String(this.Encoding.GetBytes(rawAuthor), this.Encoding));
                    //committer = new PersonIdent(new String(rawCommitter.getBytes(), encoding.name()));
                    //message = new String(readBuf, msgstart, readBuf.Length - msgstart, encoding.name());
                }
                else
                {
                    // TODO: use config setting / platform / ascii / iso-latin
                    author = new PersonIdent(rawAuthor);
                    committer = new PersonIdent(rawCommitter);
                    //message = new String(readBuf, msgstart, readBuf.Length - msgstart);
                    message = reader.ReadToEnd();
                }
            }


            raw = null;


        }

        public override string ToString()
        {
            return "Commit[" + CommitId + " " + Author + "]"; ;
        }

        /**
         * Persist this commit object
         *
         * @throws IOException
         */
        public void Save()
        {
            if (CommitId != null)
                throw new InvalidOperationException("exists " + CommitId);
            CommitId = new ObjectWriter(Repository).WriteCommit(this);
        }

    }
}
