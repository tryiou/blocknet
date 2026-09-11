Contributing to Blocknet
========================

The Blocknet project operates an open contributor model where anyone is
welcome to contribute towards development in the form of peer review,
testing and patches. This document explains the practical process and
guidelines for contributing.

The codebase is a fork of Bitcoin Core, adapted for the Blocknet
Protocol (Proof of Stake staking, XBridge atomic-swap DEX, XRouter,
Service Nodes, governance). Many docs and source filenames still use
Bitcoin Core naming (`bitcoind` → `blocknetd`, `test_bitcoin` →
`test_blocknet` sources); see `AGENTS.md` for the current repository
conventions and build instructions.

Communication Channels
----------------------

- Development happens on GitHub: open issues and pull requests at
  <https://github.com/blocknetdx/blocknet>.
- The project's website and docs: <https://blocknet.org>,
  <https://docs.blocknet.org>.
- Real-time discussion happens on the project's Discord:
  <https://discord.gg/mZ6pTneMx3>.

Contributor Workflow
--------------------

The codebase is maintained using the "contributor workflow" where
everyone without exception contributes patch proposals using "pull
requests". This facilitates social contribution, easy testing and peer
review.

To contribute a patch, the workflow is as follows:

  1. Fork repository
  2. Create topic branch
  3. Commit patches

The project coding conventions in the [developer notes](doc/developer-notes.md)
must be adhered to.

In general [commits should be atomic](https://en.wikipedia.org/wiki/Atomic_commit#Atomic_commit_convention)
and diffs should be easy to read. For this reason do not mix any formatting
fixes or code moves with actual code changes.

Commit messages should be verbose by default consisting of a short subject line
(50 chars max), a blank line and detailed explanatory text as separate
paragraph(s), unless the title alone is self-explanatory (like "Corrected typo
in init.cpp") in which case a single title line is sufficient. Commit messages should be
helpful to people reading your code in the future, so explain the reasoning for
your decisions. Further explanation [here](http://chris.beams.io/posts/git-commit/).

If a particular commit references another issue, please add the reference. For
example: `refs #1234` or `fixes #4321`. Using the `fixes` or `closes` keywords
will cause the corresponding issue to be closed when the pull request is merged.

Commit messages should never contain any `@` mentions.

Please refer to the [Git manual](https://git-scm.com/doc) for more information
about Git.

  - Push changes to your fork
  - Create pull request

The title of the pull request should be prefixed by the component or area that
the pull request affects. Valid areas as:

  - *Consensus* for changes to consensus critical code
  - *Docs* for changes to the documentation
  - *Qt* for changes to blocknet-qt
  - *Staking* for changes to the Proof of Stake code (stakemgr, kernel)
  - *XBridge* for changes to the atomic-swap DEX code
  - *XRouter* for changes to the XRouter service layer
  - *Service Nodes* for service node code
  - *Net* or *P2P* for changes to the peer-to-peer network code
  - *RPC/REST/ZMQ* for changes to the RPC, REST or ZMQ APIs
  - *Scripts and tools* for changes to the scripts and tools
  - *Tests* for changes to the unit tests or QA tests
  - *Trivial* should **only** be used for PRs that do not change generated
    executable code. Notably, refactors (change of function arguments and code
    reorganization) and changes in behavior should **not** be marked as trivial.
    Examples of trivial PRs are changes to:
    - comments
    - whitespace
    - variable names
    - logging and messages
  - *Utils and libraries* for changes to the utils and libraries
  - *Wallet* for changes to the wallet code

Examples:

    Consensus: Add new opcode for BIP-XXXX OP_CHECKAWESOMESIG
    Net: Automatically create hidden service, listen on Tor
    Qt: Add feed bump button
    XBridge: Fix fee calculation for partial orders
    Trivial: Fix typo in init.cpp

Note that translations should not be submitted as pull requests, please see
[Translation Process](doc/translation_process.md)
for more information on helping with translations.

If a pull request is not to be considered for merging (yet), please
prefix the title with [WIP] or use [Tasks Lists](https://help.github.com/articles/basic-writing-and-formatting-syntax/#task-lists)
in the body of the pull request to indicate tasks are pending.

The body of the pull request should contain enough description about what the
patch does together with any justification/reasoning. You should include
references to any discussions (for example other tickets or Discord
discussions).

At this stage one should expect comments and review from other contributors. You
can add more commits to your pull request by committing them locally and pushing
to your fork until you have satisfied all feedback.

Note: Code review is a burdensome but important part of the development process, and as such, certain types of pull requests are rejected. In general, if the **improvements** do not warrant the **review effort** required, the PR has a high chance of being rejected. It is up to the PR author to convince the reviewers that the changes warrant the review effort, and if reviewers are "Concept NAK'ing" the PR, the author may need to present arguments and/or do research backing their suggested changes.

Squashing Commits
---------------------------
If your pull request is accepted for merging, you may be asked by a maintainer
to squash and or [rebase](https://git-scm.com/docs/git-rebase) your commits.
Your contribution rules apply here as well.
