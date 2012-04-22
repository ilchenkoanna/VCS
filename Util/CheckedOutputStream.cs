using System;
using System.IO;
using ICSharpCode.SharpZipLib.Checksums;

namespace Rep.Lib {

	public class CheckedOutputStream : System.IO.Stream {
		Stream under;
		Crc32 crc;
		
		// Stream configuration: read-only, non-seeking stream
		public override bool CanRead  { get { return false; } }
		public override bool CanSeek  { get { return false; } }
		public override bool CanWrite { get { return true; } }

		public CheckedOutputStream (Stream under, Crc32 crc)
		{
			this.under = under;
			this.crc = crc;
		}
		
		public override void Flush ()
		{
			under.Flush ();
		}

		public override int Read (byte [] buffer, int offset, int count)
		{
			throw new InvalidOperationException ();
		}

		public override long Seek (long offset, SeekOrigin origin)
		{
			throw new InvalidOperationException ();
		}

		public override void SetLength (long value)
		{
			throw new InvalidOperationException ();
		}

		public override void Write (byte [] buffer, int offset, int count)
		{
			if (buffer == null)
				throw new ArgumentNullException ("buffer");

			if (offset < 0)
                                throw new ArgumentOutOfRangeException ("offset", "< 0");

                        if (count < 0)
                                throw new ArgumentOutOfRangeException ("count", "< 0");
			
                        // ordered to avoid possible integer overflow
                        if (offset > buffer.Length - count)
                                throw new ArgumentException ("Reading would overrun buffer");

			for (int i = 0; i < count; i++)
				crc.Update (buffer [i+offset]);

			under.Write (buffer, offset, count);
		}

		public override long Length {
			get {
				return under.Length;
			}
		}

		public override long Position {
			get {
				return under.Position;
			}

			set {
				throw new InvalidOperationException ();
			}
		}
	}
}