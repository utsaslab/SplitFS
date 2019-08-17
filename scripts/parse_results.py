import csv

boost_ycsb_files = ['../results/boost/LoadA/run1', '../results/boost/RunA/run1', '../results/boost/RunB/run1', '../results/boost/RunC/run1', '../results/boost/RunD/run1', '../results/boost/LoadE/run1', '../results/boost/RunE/run1', '../results/boost/RunF/run1']
nova_ycsb_files = ['../results/nova/LoadA/run1', '../results/nova/RunA/run1', '../results/nova/RunB/run1', '../results/nova/RunC/run1', '../results/nova/RunD/run1', '../results/nova/LoadE/run1', '../results/nova/RunE/run1', '../results/nova/RunF/run1']
relaxed_nova_ycsb_files = ['../results/relaxed_nova/LoadA/run1', '../results/relaxed_nova/RunA/run1', '../results/relaxed_nova/RunB/run1', '../results/relaxed_nova/RunC/run1', '../results/relaxed_nova/RunD/run1', '../results/relaxed_nova/LoadE/run1', '../results/relaxed_nova/RunE/run1', '../results/relaxed_nova/RunF/run1']
pmfs_ycsb_files = ['../results/pmfs/LoadA/run1', '../results/pmfs/RunA/run1', '../results/pmfs/RunB/run1', '../results/pmfs/RunC/run1', '../results/pmfs/RunD/run1', '../results/pmfs/LoadE/run1', '../results/pmfs/RunE/run1', '../results/pmfs/RunF/run1']
dax_ycsb_files = ['../results/dax/LoadA/run1', '../results/dax/RunA/run1', '../results/dax/RunB/run1', '../results/dax/RunC/run1', '../results/dax/RunD/run1', '../results/dax/LoadE/run1', '../results/dax/RunE/run1', '../results/dax/RunF/run1']

boost_tpcc_file = ['../results/boost/tpcc/run1']
nova_tpcc_file = ['../results/nova/tpcc/run1']
relaxed_nova_tpcc_file = ['../results/relaxed_nova/tpcc/run1']
pmfs_tpcc_file = ['../results/pmfs/tpcc/run1']
dax_tpcc_file = ['../results/dax/tpcc/run1']

boost_rsync_file = ['../results/boost/rsync/run1']
nova_rsync_file = ['../results/nova/rsync/run1']
relaxed_nova_rsync_file = ['../results/relaxed_nova/rsync/run1']
pmfs_rsync_file = ['../results/pmfs/rsync/run1']
dax_rsync_file = ['../results/dax/rsync/run1']


def get_results(system, files, check_word, rel_index):
    results = []
    results.append(system)
    for file in files:
        with open(file, 'rt') as f:
            words = f.read().split()
            for word in words:
                if check_word in word:
                    if rel_index is 0:
                        word = word.strip('0m')
                        word = word.strip('s')
                        results.append(word)
                        break
                    else:
                        results.append(words[words.index(word) + rel_index])
                        break
    return results


def write_csv(result, file, csv_header):
    with open(file, "w", newline="") as f:
        writer = csv.writer(f, delimiter=',')
        writer.writerow(csv_header)
        writer.writerows(result)


if __name__ == "__main__":
    all_results = []

    boost_results = get_results("splitfs-strict", boost_ycsb_files, "Kops/s", -1)
    nova_results = get_results("nova-strict", nova_ycsb_files, "Kops/s", -1)
    relaxed_results = get_results("nova-relaxed", relaxed_nova_ycsb_files, "Kops/s", -1)
    pmfs_results = get_results("pmfs", pmfs_ycsb_files, "Kops/s", -1)
    dax_results = get_results("ext4DAX", dax_ycsb_files, "Kops/s", -1)

    all_results.append(boost_results)
    all_results.append(nova_results)
    all_results.append(relaxed_results)
    all_results.append(pmfs_results)
    all_results.append(dax_results)

    csv_header = ['System', 'Load A', 'Run A', 'Run B', 'Run C', 'Run D', 'Load E', 'Run E', 'Run F']
    write_csv(all_results, "ycsb.csv", csv_header)

    boost_results.clear()
    nova_results.clear()
    relaxed_results.clear()
    pmfs_results.clear()
    dax_results.clear()
    all_results.clear()

    boost_results = get_results("splitfs-POSIX", boost_tpcc_file, "taken", 1)
    nova_results = get_results("nova-strict", nova_tpcc_file, "taken", 1)
    relaxed_results = get_results("nova-relaxed", relaxed_nova_tpcc_file, "taken", 1)
    pmfs_results = get_results("pmfs", pmfs_tpcc_file, "taken", 1)
    dax_results = get_results("ext4DAX", dax_tpcc_file, "taken", 1)

    for i in range(1, len(boost_results)):
        boost_results[i] = round(200000.0 / float(boost_results[i]))
        nova_results[i] = round(200000.0 / float(nova_results[i]))
        relaxed_results[i] = round(200000.0 / float(relaxed_results[i]))
        pmfs_results[i] = round(200000.0 / float(pmfs_results[i]))
        dax_results[i] = round(200000.0 / float(dax_results[i]))

    all_results.append(boost_results)
    all_results.append(nova_results)
    all_results.append(relaxed_results)
    all_results.append(pmfs_results)
    all_results.append(dax_results)

    csv_header.clear()
    csv_header = ['System', 'Throughput']
    write_csv(all_results, "tpcc.csv", csv_header)

    boost_results.clear()
    nova_results.clear()
    relaxed_results.clear()
    pmfs_results.clear()
    dax_results.clear()
    all_results.clear()

    boost_results = get_results("splitfs-sync", boost_rsync_file, "0m", 0)
    nova_results = get_results("nova-strict", nova_rsync_file, "0m", 0)
    relaxed_results = get_results("nova-relaxed", relaxed_nova_rsync_file, "0m", 0)
    pmfs_results = get_results("pmfs", pmfs_rsync_file, "0m", 0)
    dax_results = get_results("ext4DAX", dax_rsync_file, "0m", 0)

    all_results.append(boost_results)
    all_results.append(nova_results)
    all_results.append(relaxed_results)
    all_results.append(pmfs_results)
    all_results.append(dax_results)

    csv_header.clear()
    csv_header = ['System', 'Time']
    write_csv(all_results, "rsync.csv", csv_header)

    boost_results.clear()
    nova_results.clear()
    relaxed_results.clear()
    pmfs_results.clear()
    dax_results.clear()

