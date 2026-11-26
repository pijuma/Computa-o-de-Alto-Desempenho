#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
export LC_ALL=C OMP_DYNAMIC=FALSE OMP_SCHEDULE=static OMP_PROC_BIND=close OMP_PLACES=cores

if [[ $# -ne 4 ]]; then
    echo "Usage: bash run.sh SOURCE_DIR OUTPUT_DIR CONFIG_TSV pilot|size|measure" >&2
    exit 2
fi
source_dir=$(cd "$1" && pwd)
config=$(realpath "$3")
driver=$(realpath "$0")
stage=$4
case "$stage" in
    pilot) repetitions=1; workloads=(pequena) ;;
    size) repetitions=0; workloads=(grande) ;;
    measure) repetitions=5; workloads=(pequena media grande) ;;
    *) echo "Invalid stage: $stage" >&2; exit 2 ;;
esac

mkdir "$2"
output_dir=$(cd "$2" && pwd)
cd "$output_dir"
mkdir source inputs hosts runs
cp "$source_dir"/{Makefile,fire_seq.c,fire_mpi_omp.c} source/
cp "$source_dir"/data/entrada_carga_*.txt inputs/
cp "$driver" driver.sh
cp "$config" configurations.tsv
exec > >(tee driver.log) 2>&1
trap 'rc=$?; trap - EXIT; printf "%s\n" "$rc" > exit_code; find . -type f ! -name SHA256SUMS ! -name driver.log -print0 | sort -z | xargs -0 sha256sum > SHA256SUMS; exit "$rc"' EXIT

{
    date -u
    printf 'stage=%s\nrepetitions=%s\n' "$stage" "$repetitions"
    printf 'compiler_flags=-Wall -Wextra -O3 -fopenmp\n'
    hostname
    uname -a
    lscpu
    free -b
    gcc --version
    mpicc --showme
    mpirun --version
    ldd --version
    env | sort | awk '/^OMP_/ || /^OMPI_MCA_plm_rsh_args=/'
} > environment.txt
make -C source CC=gcc fire_seq fire_mpi_omp
printf 'stage\tload\tconfig\tranks\tthreads\trepetition\tkernel_seconds\tchecksum\tstatus\n' > results.tsv
errors=0

run_case() {
    local tag=$1 dir=$2 limit=$3 status
    shift 3
    mkdir -p "$dir"
    printf '%q ' "$@" > "$dir/command.sh"
    printf '\n' >> "$dir/command.sh"
    if [[ -x /usr/bin/time ]]; then
        if /usr/bin/time -v -o "$dir/resources.txt" timeout "$limit" "$@" < /dev/null > "$dir/stdout" 2> "$dir/stderr"; then status=0; else status=$?; fi
    else
        if timeout "$limit" "$@" < /dev/null > "$dir/stdout" 2> "$dir/stderr"; then status=0; else status=$?; fi
    fi
    printf '%s\n' "$status" > "$dir/exit_code"
    printf '%s exit=%s\n' "$tag" "$status"
    return "$status"
}

launch_args() {
    launcher=(mpirun -x OMP_DYNAMIC -x OMP_SCHEDULE -x OMP_PROC_BIND -x OMP_PLACES --hostfile "$hostfile" --map-by "ppr:${rpn}:node:PE=${threads}" --bind-to core --report-bindings -np "$ranks")
}

printf '1 7 0 2 13 100\n0 1 0\n0 0\n' > inputs/zero.txt
run_case zero-baseline runs/zero-baseline 60 "$output_dir/source/fire_seq" "$output_dir/inputs/zero.txt"
while IFS=$'\t' read -r label ranks threads rpn hostpath; do
    [[ $label =~ ^[A-Za-z0-9_-]+$ && $ranks =~ ^[1-9][0-9]*$ && $threads =~ ^[1-9][0-9]*$ && $rpn =~ ^[1-9][0-9]*$ ]] || exit 2
    cp "$hostpath" "hosts/$label"
    hostfile="$output_dir/hosts/$label"
    launch_args
    if ! run_case "$label-placement" "runs/$label-placement" 60 "${launcher[@]}" hostname; then
        errors=1
        continue
    fi
    [[ $(sort -u "runs/$label-placement/stdout" | wc -l) -eq $(((ranks+rpn-1)/rpn)) ]] || {
        echo "Unexpected node placement: $label" >&2
        errors=1
        continue
    }
    awk -v t="$threads" 'NR==1 {$4=t} 1' inputs/zero.txt > "inputs/zero-$label.txt"
    if ! run_case "$label-zero" "runs/$label-zero" 60 "${launcher[@]}" "$output_dir/source/fire_mpi_omp" "$output_dir/inputs/zero-$label.txt" ||
       ! diff -u <(head -11 runs/zero-baseline/stdout) <(head -11 "runs/$label-zero/stdout") > "runs/$label-zero/diff"; then
        errors=1
        continue
    fi
    if run_case "$label-invalid" "runs/$label-invalid" 60 "${launcher[@]}" "$output_dir/source/fire_mpi_omp" "$output_dir/inputs/absent"; then
        errors=1
        continue
    fi
    if [[ $(cat "runs/$label-invalid/exit_code") != 1 || -s "runs/$label-invalid/stdout" ]] ||
       ! grep -q 'Erro ao abrir' "runs/$label-invalid/stderr"; then
        errors=1
        continue
    fi
    printf '%s\t%s\t%s\t%s\t%s\n' "$label" "$ranks" "$threads" "$rpn" "$hostfile" >> validated.tsv
done < configurations.tsv
[[ -s validated.tsv ]]
if [[ $stage == measure && $errors != 0 ]]; then exit 1; fi

for load in "${workloads[@]}"; do
    awk 'NR==1 {$4=1} 1' "inputs/entrada_carga_$load.txt" > "inputs/$load-seq.txt"
    for ((rep=0; rep<=repetitions; rep++)); do
        oracle="runs/$load-seq-$rep"
        if ! run_case "$load-seq-$rep" "$oracle" 600 "$output_dir/source/fire_seq" "$output_dir/inputs/$load-seq.txt"; then
            errors=1
            continue
        fi
        if [[ $(wc -l < "$oracle/stdout") != 12 ]]; then errors=1; continue; fi
        printf '%s\t%s\tseq\t1\t1\t%s\t%s\t%s\tok\n' "$stage" "$load" "$rep" "$(awk '/^tempo:/{print $2}' "$oracle/stdout")" "$(awk '/^checksum:/{print $2}' "$oracle/stdout")" >> results.tsv
        while IFS=$'\t' read -r label ranks threads rpn hostfile; do
            launch_args
            input="$output_dir/inputs/$load-$label.txt"
            awk -v t="$threads" 'NR==1 {$4=t} 1' "inputs/entrada_carga_$load.txt" > "$input"
            run="runs/$load-$label-$rep"
            status=ok
            if ! run_case "$load-$label-$rep" "$run" 600 "${launcher[@]}" "$output_dir/source/fire_mpi_omp" "$input" ||
               [[ $(wc -l < "$run/stdout") != 12 ]] ||
               ! diff -u <(head -11 "$oracle/stdout") <(head -11 "$run/stdout") > "$run/diff"; then
                status=failed
                errors=1
            fi
            printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$stage" "$load" "$label" "$ranks" "$threads" "$rep" "$(awk '/^tempo:/{print $2}' "$run/stdout")" "$(awk '/^checksum:/{print $2}' "$run/stdout")" "$status" >> results.tsv
        done < validated.tsv
    done
done
exit "$errors"
