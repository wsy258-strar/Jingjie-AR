export class GyroController {
  constructor({ adapter, deviceOrientation = globalThis.DeviceOrientationEvent, onDenied = () => {} } = {}) {
    this.adapter = adapter;
    this.deviceOrientation = deviceOrientation;
    this.onDenied = typeof onDenied === "function" ? onDenied : () => {};
    this.permissionRequested = false;
    this.permissionGranted = false;
    this.suspensions = new Set();
    this.gyroEnabled = false;
    this.destroyed = false;
  }

  isAvailable() {
    return Boolean(this.adapter && this.adapter.isGyroAvailable && this.adapter.isGyroAvailable());
  }

  enableIfAllowed() {
    if (this.destroyed || !this.permissionGranted || this.suspensions.size || !this.isAvailable()) return false;
    if (!this.gyroEnabled) {
      this.adapter.enableGyro();
      this.gyroEnabled = true;
    }
    return true;
  }

  async requestFromGesture() {
    if (this.destroyed || !this.isAvailable()) return false;
    if (this.permissionRequested) return this.enableIfAllowed();

    this.permissionRequested = true;
    const requestPermission = this.deviceOrientation && this.deviceOrientation.requestPermission;
    try {
      const permission = typeof requestPermission === "function"
        ? await requestPermission.call(this.deviceOrientation)
        : "granted";
      this.permissionGranted = permission === "granted";
    } catch (error) {
      this.permissionGranted = false;
    }
    if (!this.permissionGranted) {
      this.onDenied();
      return false;
    }
    return this.enableIfAllowed();
  }

  async autoEnable() {
    if (this.deviceOrientation && typeof this.deviceOrientation.requestPermission === "function") return false;
    return this.requestFromGesture();
  }

  suspend(reason) {
    this.suspensions.add(reason);
    if (this.gyroEnabled) {
      this.adapter.disableGyro();
      this.gyroEnabled = false;
    }
  }

  resume(reason) {
    this.suspensions.delete(reason);
    return this.enableIfAllowed();
  }

  destroy() {
    if (this.destroyed) return;
    this.destroyed = true;
    this.suspensions.clear();
    if (this.gyroEnabled) this.adapter.disableGyro();
    this.gyroEnabled = false;
  }
}
