# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

-

### Changed

-

### Removed

-

## [0.2.0] - 2024-02-27

### Added

- Overrun policy for Ring Buffer Readers.
- Ring Buffer Reader abort support.
- Ring Buffer Reader de-init function: 'mrrb_reader_deinit'.
- Function to get the amount of overwritable space in the buffer: 'mrrb_get_overwritable_space'.
- Abort functions for mrrb_retarget example.

### Changed

- Makefile: Compiler optimization flags for test and coverage build.
- Makefile: Print Code coverage log after coverage testing.

### Removed

-

## [0.1.0] - 2024-02-27

### Added

- Initial version of the Multi-Reader Ring Buffer