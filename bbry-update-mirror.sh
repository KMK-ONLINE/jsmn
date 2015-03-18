#!/bin/sh
#
# Script to mirror a mercurial repository.
# =======================================
#
# NB: this script is self-bootstrapping.  To bootstrap, do the following:
#  1. mkdir git-${repo}-mirror
#  2. cd !^
#  3. path/to/bbry-update-mirror.sh
#  4. git push $origin '+refs/heads/*:refs/heads/*' '+refs/meta/*:refs/meta/*' '+refs/notes/*:refs/notes/*'
#
# When this repository has been cloned, simply re-run this script to
# update the mirror.  Push again using the above.

this_file="$0"
hg_mirror_dir="hg-repo-to-mirror"

die () {
	echo "$*" >&2
	exit 1
}

# how to init this repository
setup_git () {
	git init --quiet
	git_dir=$(git rev-parse --git-dir)
	#git config --replace-all core.logAllRefUpdates false

	git symbolic-ref HEAD refs/heads/master
	this_file_basename="$(basename ${this_file})"
	cp "${this_file}" ./
	GIT_WORK_TREE=$PWD git add "${this_file_basename}"
	GIT_WORK_TREE=$PWD git commit --quiet -m "import mirror tools"
	git symbolic-ref HEAD refs/heads/master
	rm "${this_file_basename}"

	# delete the index
	rm "${git_dir}/index"

	# return the git dir name
	echo "${git_dir}"
}

hg_clone () {
	hg clone -U https://bitbucket.org/zserge/jsmn "${hg_mirror_dir}" \
	|| die "hg clone failed"

	SUBDIRECTORY_OK=1 \
		hg-fast-export.sh -r hg-repo-to-mirror \
			--quiet --hg-hash --hgtags -o mirror

	# for sanity, checkout the "master"
	GIT_WORK_TREE=../ git reset --hard
}

hg_fetch () {
	# workout any ref prefix to use:
	#  - if the git repo already has an 'origin', use that
	# otherwise, assume a mirror copy
	git config remote.origin.url >/dev/null \
	&& hg_origin_opt="-o origin"

	hg -R "${hg_mirror_dir}" pull
	git config --replace-all core.bare true
	SUBDIRECTORY_OK=1 \
		hg-fast-export.sh -r hg-repo-to-mirror \
			--quiet --hg-hash --hgtags -o mirror
	git config --unset core.bare
}

setup_hg_fast_export () {
	PATH="$PATH:$PWD/hg-fast-export/"
	export PATH

	if [ -z "$(which hg-fast-export.sh)" ]
	then
		# get a copy of the fast-export command
		git clone http://repo.or.cz/fast-export.git hg-fast-export
	fi

	# double check that hg-fast-export can be found
	which hg-fast-export.sh 2>&1 >/dev/null \
	|| die "failed to find hg-fast-export"
}

# clean up function for hg metadata handling
meta_commit () {
	saved_HEAD=$(git symbolic-ref HEAD)
	# remove stale files, point HEAD to branch we will commit to
	rm -f COMMIT_EDITMSG
	git symbolic-ref HEAD refs/meta/hg
	## deleting the following will result in a new metadata push each time
	# git update-ref -d refs/meta/hg

	# update the index with the metadata tree, check for differences
	(
		export GIT_WORK_TREE="."
		export GIT_INDEX_FILE="hg_meta_index"
		git add hg2git-heads hg2git-mapping hg2git-marks hg2git-state
		git diff-index --quiet --exit-code refs/meta/hg -- \
		|| (
			git update-ref -d refs/meta/hg
			git commit --quiet -m "git-hg metadata"
		)
		rm -f hg_meta_index COMMIT_EDITMSG
	)

	git symbolic-ref HEAD "${saved_HEAD}"
}

# checkout hg meta data
meta_checkout () {
	# find where the meta data is:
	#  - refs/meta/origin/hg (if cloned)
	#  - refs/meta/hg
	#  - otherwise, fetch from origin

	for ref in refs/meta/origin/hg refs/meta/hg
	do
		git show-ref --quiet --verify "${ref}" \
		&& meta_ref="${ref}" \
		&& break
	done

	# can't proceed if origin doesn't exist
	git config remote.origin.url >/dev/null || return

	if [ -z "${meta_ref}" ]
	then
		git fetch origin meta/hg:refs/meta/origin/hg \
		&& meta_ref="refs/meta/origin/hg" \
		|| return
	fi

	GIT_INDEX_FILE="hg_meta_index" \
	GIT_WORK_TREE="${git_dir}" \
		git checkout "${meta_ref}" -- .
}

##
## BEGIN
##

# find .git dir to do work inside (initialising if necessary)
git_dir="$(git rev-parse --git-dir || setup_git)"
cd "${git_dir}"

# sort out tooling
setup_hg_fast_export

# if hg clone directory doesn't exist, clone the repository
[ -d "${hg_mirror_dir}" ] || hg_clone

# fetch any updates and store updated metadata
meta_checkout
hg_fetch
meta_commit
