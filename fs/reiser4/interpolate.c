/* We will use @ as the symbol for dereferencing, we won't use * because
we want to reserve it for use as a wildcard someday.

Inheriting stat data from source_filename can be done as:

target_filename/mode<=@source_filename/mode

File body inheritance is accomplished by extending symlink functionality:

file_body_inheritance example:

target_filename/symlink<=`@freshly_interpolate_this_filename_whenever_resolving_target_filename+`here is some text stored directly in the symlink''+@interpolate_this_filename_at_symlink_creation_time+`@freshly_interpolate_this_filename2_whenever_resolving_target_filename+"this is some more text that is directly embedded in the symlink"'

Mr. Demidov, flesh this out in detail, being careful to worry about
how to write to interpolated files.  I think you need to interpret
strings that are between interpolations as the delimiters of those
interpolations, and changing those strings can then only be done by
writing to filename/sym.

*/
