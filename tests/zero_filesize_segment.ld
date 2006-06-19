PHDRS
{
  headers PT_PHDR PHDRS ;
  interp PT_INTERP ;
  text PT_LOAD FILEHDR PHDRS ;
  data PT_LOAD ;
  empty PT_LOAD ;
  dynamic PT_DYNAMIC ;
  note PT_NOTE ;
  gnu_stack PT_GNU_STACK ;
}
SECTIONS
{
	.interp : { } :interp :text
	.hash : { } :text
	.tdata : { } :data
	.dynamic : { } :dynamic :data
	.got : { } :data
	.plt : { } :data
	.data : { } :data
}
INPUT( dummy.o );
INPUT( testutils.o );
INPUT( -lc );
