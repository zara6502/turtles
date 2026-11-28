/*
 * Ross Data Compression
 *
 * This program implements a simple (and fast) Data Compression Scheme
 * devised by Ed Ross, and published in the Oct/1992 'C' Users Journal.
 *
 * Compile command: cc rdc -fop
 */
#include <stdio.h>

#define	HASH_LEN	4096		/* Number of hash table entries */
#define	BUFF_LEN	16384		/* Buffer length */

/*
 * Ross Data Compression - Compress functions
 *
 * Compress input buffer -> output buffer and return length of output
 * buffer. Negative length indicates that buffer could not be compressed
 */
int rdc_compress(inbuff, inbuff_len, outbuff)
	unsigned char	*inbuff, *outbuff;
	unsigned int	inbuff_len;
{
	unsigned char	*in_idx, *inbuff_end, *anchor, *pat_idx, *out_idx,
					*outbuff_end, *hash_tbl[HASH_LEN];
	unsigned int	cnt, gap, c, hash, *ctrl_idx, ctrl_bits, ctrl_cnt;

	inbuff_end = (in_idx = inbuff) + inbuff_len;
	ctrl_idx = (unsigned int *) outbuff;
	ctrl_cnt = 0;

	out_idx = outbuff + sizeof(unsigned int);
	outbuff_end = (inbuff_len - 48) + outbuff;

	/* Skip compression for small buffers */
	if(inbuff_len <= 18) {
		memcpy(outbuff, inbuff, inbuff_len);
		return 0 - inbuff_len; }

	/* Scan through inbuff */
	while(in_idx < inbuff_end) {
		/* Make room for control bits & check for outbuff overflow */
		if(ctrl_cnt++ == 16) {
			*ctrl_idx = ctrl_bits;
			ctrl_cnt = 1;
			ctrl_idx = (unsigned int *) out_idx;
			out_idx += 2;
			if(out_idx > outbuff_end) {
				memcpy(outbuff, inbuff, inbuff_len);
				return 0 - inbuff_len; } }

		/* Look for RLE */
		c = *(anchor = in_idx++);
		while((in_idx < inbuff_end) && (*in_idx == c) && ((in_idx - anchor) < 4114))
			++in_idx;

		/* Store compression code if character is repeated 2 or more times */
		if((cnt = in_idx - anchor) > 2) {
			if(cnt <= 18) {			/* Short RLE */
				*out_idx++ = cnt - 3;
				*out_idx++ = c; }
			else {					/* Long RLE */
				*out_idx++ = ((cnt -= 19) & 0x0F) + 16;
				*out_idx++ = cnt >> 4;
				*out_idx++ = c; }
			ctrl_bits = (ctrl_bits << 1) | 1;
			continue; }

		/* Look for pattern if 2 or more character remain in input buffer */
		in_idx = anchor;
		if((inbuff_end - in_idx) > 2) {
			/* Locate offset of possible pattern in sliding dictionary */
			hash = ((((in_idx[0] & 15) << 8) | in_idx[1])
				^ ((in_idx[0] >> 4) | (in_idx[2] << 4))) & (int)(HASH_LEN-1);
			pat_idx = hash_tbl[hash];
			hash_tbl[hash] = in_idx;

		/* Compare characters if we are within 4098 bytes */
		if((gap = in_idx - pat_idx) <= 4098) {
			while((in_idx < inbuff_end) && (pat_idx < anchor)
			  && (*pat_idx == *in_idx) && ((in_idx - anchor) < 271)) {
				++in_idx;
				++pat_idx; }
			/* Store pattern of more than 2 characters */
			if((cnt = in_idx - anchor) > 2) {
				gap -= 3;
				if(cnt <= 15) {			/* Short pattern */
					*out_idx++ = (cnt << 4) + (gap & 0x0F);
					*out_idx++ = gap >> 4; }
				else {					/* Long pattern */
					*out_idx++ = (gap & 0x0F) + 32;
					*out_idx++ = gap >> 4;
					*out_idx++ = cnt - 16; }
				ctrl_bits = (ctrl_bits << 1) | 1;
				continue; } } }

		/* Can't compress this character - copy to outbuff */
		*out_idx++ = c;
		in_idx = ++anchor;
		ctrl_bits <<= 1; }

	/* Save last batch of control bits */
	ctrl_bits <<= (16 - ctrl_cnt);
	*ctrl_idx = ctrl_bits;

	/* Return size of compressed buffer */
	return out_idx - outbuff;
}

/*
 * Ross Data Compression - Decompress functions
 *
 * Expand input buffer -> output buffer and return length of output
 * buffer.
 */
int rdc_decompress(inbuff, inbuff_len, outbuff)
	unsigned char	*inbuff, *outbuff;
	unsigned int	inbuff_len;
{
	unsigned int	ctrl_bits, ctrl_mask, cmd, cnt, ofs;
	unsigned char	*inbuff_idx, *outbuff_idx, *inbuff_end;

	ctrl_mask = 0;
	inbuff_end = (inbuff_idx = inbuff) + inbuff_len;
	outbuff_idx = outbuff;

	/* Process each item in inbuff */
	while(inbuff_idx < inbuff_end) {
		/* Get new load of control bits if needed */
		if(!(ctrl_mask >>= 1)) {
			ctrl_bits = *(unsigned int *)inbuff_idx;
			inbuff_idx += 2;
			ctrl_mask = 0x8000; }

		/* Just copy this character is control bit is zero */
		if(!(ctrl_bits & ctrl_mask)) {
			*outbuff_idx++ = *inbuff_idx++;
			continue; }

	/* Undo the compression code */
	cnt = (*inbuff_idx & 0x0f) + 3;
	switch(cmd = (*inbuff_idx++ >> 4) & 0x0f) {
		case 0 :		/* Short RLE */
			memset(outbuff_idx, *inbuff_idx++, cnt);
			outbuff_idx += cnt;
			break;
		case 1 :		/* Long RLE */
			cnt += (*inbuff_idx++ << 4) + 16;
			memset(outbuff_idx, *inbuff_idx++, cnt);
			outbuff_idx += cnt;
			break;
		case 2 :		/* Long pattern */
			ofs = (*inbuff_idx++ << 4) + cnt;
			memcpy(outbuff_idx, outbuff_idx - ofs, cnt = *inbuff_idx ++ + 16);
			outbuff_idx += cnt;
			break;
		default :		/* Short pattern */
			ofs = (*inbuff_idx++ << 4) + cnt;
			memcpy(outbuff_idx, outbuff_idx - ofs, cmd);
			outbuff_idx += cmd; } }

	/* Return length of decompressed buffer */
	return outbuff_idx - outbuff;
}

/*
 * Test program
 */
main(argc, argv)
	int argc;
	char *argv[];
{
	int isize, csize;
	unsigned char inbuff[BUFF_LEN], outbuff[BUFF_LEN];

	FILE *fpi, *fpo;

	switch((argc == 4) ? toupper(*argv[1]) : 0) {
		case 'C' :		/* Compress infile to outfile */
			fpi = fopen(argv[2], "rvqb");
			fpo = fopen(argv[3], "wvqb");
			while(isize = fget(inbuff, BUFF_LEN, fpi)) {
				csize = rdc_compress(inbuff, isize, outbuff);
				if(fput(&csize, sizeof(int), fpo) != sizeof(int))
					abort("Can't write size");
				if(csize < 0)
					csize = 0 - csize;
				if(fput(outbuff, csize, fpo) != csize)
					abort("Can't write buffer"); }
			if(fput(&isize, sizeof(int), fpo) != sizeof(int))
				abort("Can't write END flag");
			fclose(fpi);
			fclose(fpo);
			break;
		case 'D' :		/* Decompress infile to outfile */
			fpi = fopen(argv[2], "rvqb");
			fpo = fopen(argv[3], "wvqb");
			for(;;) {
				if(fget(&isize, sizeof(int), fpi) != sizeof(int))
					abort("Can't read size");
				if(!isize)
					break;
				if(isize < 0) {	/* Uncompressed block - pass through */
					csize = 0 - isize;
					if(fget(outbuff, csize, fpi) != csize)
						abort("Can't read ublock"); }
				else {			/* Compressed block - decompress */
					if(fget(inbuff, isize, fpi) != isize)
						abort("Can't read cblock");
					csize = rdc_decompress(inbuff, isize, outbuff); }
				if(fput(outbuff, csize, fpo) != csize)
					abort("Can't write output file"); }
			fclose(fpi);
			fclose(fpo);
			break;
		default:
			abort("\nUse: rdc Compress|Decompress <infile> <outfile>\n"); }

}
