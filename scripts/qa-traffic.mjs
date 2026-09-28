/** Offline integrity proof; live provider coverage must be verified with an owner-supplied key. */
import { spawnSync } from 'node:child_process';
const result = spawnSync(process.execPath, ['--test', 'src/layers/traffic/navigation.test.mjs', 'src/layers/traffic/rendering.test.mjs', 'src/data/traffic.test.mjs'], { stdio: 'inherit' });
process.exitCode = result.status ?? 1;
