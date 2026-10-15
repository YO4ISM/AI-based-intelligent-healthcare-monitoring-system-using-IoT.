import numpy as np
import pandas as pd
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import LabelEncoder, StandardScaler
import tensorflow as tf

# 1. تحميل الداتاسيت
# استبدل 'vitals_data.csv' باسم ملفك الفعلي
df = pd.read_csv("Health data.csv")

print("نظرة أولية على البيانات:")
print(df.head())

# حدد أسماء الأعمدة الخاصة بك بدقة
# حدد أسماء الأعمدة الفعلية من ملفك
feature_cols = ['pulse', 'body temperature', 'SpO2']
target_col = 'Status'                                  # عمود الحالة

X = df[feature_cols].values
y_raw = df[target_col].values

# 2. ترميز عمود الحالة (Label Encoding) إذا كان نصياً
encoder = LabelEncoder()
y = encoder.fit_transform(y_raw)
num_classes = len(encoder.classes_)
print(f"\nالفئات المسجلة: {dict(enumerate(encoder.classes_))}")

# 3. تطبيع المدخلات (Scaling)
# خطوة حاسمة جداً: سنحتاج لحفظ المتوسط (mean) والانحراف (scale) لنستخدمهما لاحقاً في كود الـ ESP32
scaler = StandardScaler()
X = scaler.fit_transform(X)

print("\n--- ثوابت التطبيع (Normalization Constants) ---")
print(f"المتوسطات (Means): {scaler.mean_}")
print(f"الانحراف المعياري (Std/Scale): {scaler.scale_}")
print("احفظ هذه الأرقام، سنحتاجها لمعادلة الدخل داخل الـ ESP32!")

# تقسيم البيانات للتدريب والاختبار
X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.2, random_state=42, stratify=y
)

# 4. بناء بنية الشبكة العصبية (MLP خفيفة ومناسبة جداً للـ TinyML)
model = tf.keras.Sequential([
    tf.keras.layers.Input(shape=(3,)),
    tf.keras.layers.Dense(16, activation='relu'),
    tf.keras.layers.Dense(8, activation='relu'),
    tf.keras.layers.Dense(num_classes, activation='softmax' if num_classes > 2 else 'sigmoid')
])

loss_func = 'sparse_categorical_crossentropy' if num_classes > 2 else 'sparse_categorical_crossentropy'
model.compile(optimizer='adam', loss=loss_func, metrics=['accuracy'])

# 5. التدريب
history = model.fit(
    X_train, y_train,
    validation_data=(X_test, y_test),
    epochs=50,
    batch_size=16,
    verbose=1
)

loss, acc = model.evaluate(X_test, y_test, verbose=0)
print(f"\nدقة النموذج على بيانات الاختبار: {acc * 100:.2f}%")

# 6. التكميم الكامل إلى int8 (Full Integer Quantization) لـ ESP32
def representative_dataset():
    # تزويد المكمم بعينات من البيانات لضبط نطاق الأعداد الصحيحة بدقة
    for i in range(len(X_train)):
        yield [X_train[i:i+1].astype(np.float32)]

converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset

# إجبار التكميم الكامل للمدخلات والمخرجات ليعمل بأقصى سرعة على العتاد
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

tflite_quant_model = converter.convert()

with open("model_quant.tflite", "wb") as f:
    f.write(tflite_quant_model)

print("\nتم تصدير النموذج المكمم: model_quant.tflite بنجاح!")

# 7. تحويل ملف tflite إلى ملف هيدر C++ مباشرة (بدون الحاجة لأداة xxd خارجية)
def convert_to_c_array(tflite_bytes, output_header_file):
    with open(output_header_file, "w") as f:
        f.write("#ifndef MODEL_DATA_H_\n#define MODEL_DATA_H_\n\n")
        f.write(f"const unsigned int g_model_len = {len(tflite_bytes)};\n")
        f.write("const unsigned char g_model[] __attribute__((aligned(4))) = {\n  ")
        for i, val in enumerate(tflite_bytes):
            f.write(f"0x{val:02x}, ")
            if (i + 1) % 12 == 0:
                f.write("\n  ")
        f.write("\n};\n\n#endif // MODEL_DATA_H_\n")

convert_to_c_array(tflite_quant_model, "model_data.h")
print("تم إنشاء ملف 'model_data.h' بنجاح وجاهز للنقل إلى مشروع الـ ESP32!")