# [JamLoop](http://jamloop.com/)

![logo](https://media.licdn.com/media/p/3/000/2a8/17e/0f7d6dd.png "JamLoop")

For any details on this repository, contact [support@datacratic.com](support@datacratic.com).

## [RTBkit](http://rtbkit.org/)

To be able to update to a new version or publish to RTBkit, you'll need a fork on [github](https://github.com/). This fork will be public and will provide a way to sync back and forth with the official one.

### Pull & Push

This repository contains a subtree of RTBkit i.e. all commits. Updating it to a new version means importing commits from the updated fork of RTBkit or from RTBkit directly. This can be done with subtree pull:

```
git subtree pull --prefix rtbkit https://github.com/your-fork/rtbkit.git master --squash
```

Sometimes, it's useful to be able to upstream changes to RTBkit. The first step is to extract and upstream commits (with their history limited to rtbkit) to your fork. This can be done with subtree pull:

```
git subtree push --prefix rtbkit https://github.com/your-fork/rtbkit.git master
```

Then, create a pull request and submit it for review.

### Build

RTBkit comes with its own build system i.e. jml-build. There is a makefile at the root that allows building RTBkit and running RTBkit test suite.

```
make rtbkit
make rtbkit-tests
```

### Operations

There are a few scripts supplied with this installation to ease deployments. The user `rtbkit` has a `prod` folder in its `home` directory. That folder contains 2 complete versions of the codebase called `rtb-black` and `rtb-white`. There is also a symbolic link `rtb` that points to the version currently in production i.e. either `rtb-black` or `rtb-white`.

Under normal operations, deployment happens in the color that's not in production. The script uses the `prod` branch unless one is specified. The branch is fetched, checkout and the code is built. Beware that any pending modifications will be lost.

```
deploy-rtb COLOR [--branch NAME]
```

Once deployed, you can restart the stack on that new color. Note that if nothing is specified, the script will restart the stack on the currently active color.

```
launch-rtb [COLOR]
```

If deployment failed for whatever reason, you can quickly revert to the previous color.

```
launch-rtb other
```
