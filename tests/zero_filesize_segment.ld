SECTIONS
{
	.empty (0x90000000) : {
		__empty_segment = .;
		. = . + 4;
	}
}
