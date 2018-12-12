mkdir dir
mkdir dir/subdir1
mkdir dir/subdir2
echo hello1 > dir/file1
echo hello2 > dir/file2
echo hello > dir/subdir1/file1
ln -s dir/file1 dir/link1
ln -s dir/subdir1 dir/linkdir1
echo gotcha > dir/subdir1/file_to_find
echo gotcha > dir/subdir1/another_file_to_find

