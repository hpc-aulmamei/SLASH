Contributing to SLASH
======================

We welcome contributions to SLASH! You can contribute to SLASH in a variety of ways. You can report bugs and feature requests using [GitHub Issues](https://github.com/Xilinx/SLASH/issues). You can send patches which add new features to, or fix bugs in SLASH. We also encourage sending patches to update [SLASH documentation](https://slash-fpga.readthedocs.io/).


Reporting Issues
****************

When reporting issues please include as many as possible of the following items:

1. A summary description of the issue
2. SLASH git hash
3. [For HW bugs] Output of ``v80-smi list``
4. Output of ``v80-smi inspect`` on your `vrtbin`
5. Output of ``dmesg``
6. Instructions how to reproduce

Contributing Code
*****************

Please use GitHub Pull Requests (PR) for sending code contributions. Only target the `dev` branch with your PRs. When sending code sign your work as described below. Be sure to use the same license for your contributions as the current license of the SLASH component you are contributing to.


Sign Your Work
==============

Please use the *Signed-off-by* line at the end of your patch which indicates that you accept the Developer Certificate of Origin (DCO) defined by https://developercertificate.org/ reproduced below::

```
  Developer Certificate of Origin
  Version 1.1

  Copyright (C) 2004, 2006 The Linux Foundation and its contributors.
  1 Letterman Drive
  Suite D4700
  San Francisco, CA, 94129

  Everyone is permitted to copy and distribute verbatim copies of this
  license document, but changing it is not allowed.


  Developer's Certificate of Origin 1.1

  By making a contribution to this project, I certify that:

  (a) The contribution was created in whole or in part by me and I
      have the right to submit it under the open-source license
      indicated in the file; or

  (b) The contribution is based upon previous work that, to the best
      of my knowledge, is covered under an appropriate open-source
      license and I have the right under that license to submit that
      work with modifications, whether created in whole or in part
      by me, under the same open-source license (unless I am
      permitted to submit under a different license), as indicated
      in the file; or

  (c) The contribution was provided directly to me by some other
      person who certified (a), (b) or (c) and I have not modified
      it.

  (d) I understand and agree that this project and the contribution
      are public and that a record of the contribution (including all
      personal information I submit with it, including my sign-off) is
      maintained indefinitely and may be redistributed consistent with
      this project or the open source license(s) involved.
```

You can enable Signed-off-by automatically by adding the `-s` flag to the `git commit` command.

Here is an example Signed-off-by line which indicates that the contributor accepts DCO:

```
  This is my commit message

  Signed-off-by: Jane Doe <jane.doe@example.com>
```

Code License
============

All SLASH code is licensed under the terms [LICENSE](https://github.com/Xilinx/SLASH/blob/main/LICENSE) Your contribution will be accepted under the same license.

Please consult the table below for the brief summary of SLASH license for various components.


| Component           |  License     |
|---------------------|--------------|
| Linux vrt driver    |  GPLv2       |
| User space          |  MIT         |
