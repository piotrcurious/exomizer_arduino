# Compression & Decompression Test Report for test_fies/

This directory contains additional test files for validating the Exomizer-compatible decompressor.

## Test Results (Improved Compressor)

| File | Preset | Mode | Original | Compressed | Ratio | Status |
| --- | --- | --- | --- | --- | --- | --- |
| test_fies/Prometheus.txt | speed | block | 124729 | 125584 | 100.69% | PASS |
| test_fies/Prometheus.txt | speed | streaming | 124729 | 125584 | 100.69% | PASS |
| test_fies/Prometheus.txt | balanced | block | 124729 | 124059 | 99.46% | PASS |
| test_fies/Prometheus.txt | balanced | streaming | 124729 | 124059 | 99.46% | PASS |
| test_fies/Prometheus.txt | ratio | block | 124729 | 124059 | 99.46% | PASS |
| test_fies/Prometheus.txt | ratio | streaming | 124729 | 124059 | 99.46% | PASS |
| test_fies/Prometheus48+128.txt | speed | block | 20693 | 20479 | 98.97% | PASS |
| test_fies/Prometheus48+128.txt | speed | streaming | 20693 | 20479 | 98.97% | PASS |
| test_fies/Prometheus48+128.txt | balanced | block | 20693 | 20401 | 98.59% | PASS |
| test_fies/Prometheus48+128.txt | balanced | streaming | 20693 | 20401 | 98.59% | PASS |
| test_fies/Prometheus48+128.txt | ratio | block | 20693 | 20401 | 98.59% | PASS |
| test_fies/Prometheus48+128.txt | ratio | streaming | 20693 | 20401 | 98.59% | PASS |

## Summary

All test cases for files in `test_fies/` passed successfully. The improved compressor yields better ratios compared to the initial version.
