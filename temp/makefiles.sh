mkdir dir
mkdir dir/subdir1
mkdir dir/subdir2
echo hello > dir/file1
echo hello > dir/subdir1/file1
ln -s dir/file1 link1
ln -s dir/subdir1 linkdir1
